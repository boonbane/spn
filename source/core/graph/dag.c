#include "sp.h"
#include "io/io.h"
#include "macro/macro.h"
#include "project/project.h"
#include "ctx/types.h"
#include "error/error.h"
#include "spn/errors.h"
#include "event/types.h"
#include "core/types.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "core/core.h"
#include "cpu/cpu.h"
#include "dag/wasi/canonicalize.h"
#include "enum/enum.h"
#include "event/event.h"
#include "external/wasm/wasm.h"
#include "op/op.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "session/session.h"
#include "thread_pool/thread_pool.h"
#include "unit/unit.h"
#include "graph/build.h"
#include "toolchain/linker.h"
#include "graph/dag.h"
#include "graph/identity.h"
#include "graph/nodes/nodes.h"
#include "triple/triple.h"
#include "unit/package.h"

typedef struct {
  spn_target_unit_t* target;
  sp_da(spn_dag_id_t) objects;
  spn_dag_id_t exports;
  bool implib;
} spn_dag_link_ctx_t;

static spn_path_t dag_artifact_path(spn_dag_t* g, spn_dag_id_t id) {
  return spn_dag_find_artifact(g, id)->materialized;
}

static spn_path_t dag_artifact_declared(spn_dag_t* g, spn_dag_id_t id) {
  return spn_dag_find_artifact(g, id)->path;
}

static sp_da(spn_path_t) dag_declared_paths(sp_mem_t mem, spn_dag_t* g, sp_da(spn_dag_id_t) ids) {
  sp_da(spn_path_t) paths = sp_da_new(mem, spn_path_t);
  sp_da_reserve(paths, sp_da_size(ids));
  sp_da_for(ids, it) {
    sp_da_push(paths, dag_artifact_declared(g, ids[it]));
  }
  return paths;
}

static spn_dag_digest_t hash_embedding(spn_target_unit_t* target) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.embed.v9"));
  spn_dag_hash_str(&ctx, target->pkg->info->qualified);
  spn_dag_hash_str(&ctx, target->info->name);
  spn_dag_hash_u8(&ctx, (u8)target->info->kind);
  spn_profile_info_t* profile = &target->pkg->build->profile;
  spn_dag_hash_u8(&ctx, (u8)profile->os);
  spn_dag_hash_u8(&ctx, (u8)profile->arch);
  spn_dag_hash_u8(&ctx, (u8)profile->abi);
  sp_da_for(target->info->embed, it) {
    spn_embed_t* embed = &target->info->embed[it];
    spn_dag_hash_u8(&ctx, (u8)embed->kind);
    spn_dag_hash_path(&ctx, embed->path);
    spn_dag_hash_str(&ctx, embed->dest);
    spn_dag_hash_str(&ctx, embed->types.data);
    spn_dag_hash_str(&ctx, embed->types.size);
  }
  return spn_dag_hash_final(&ctx);
}

static spn_err_t compile_object(sp_mem_t scratch, spn_dag_t* g, spn_compile_unit_t* unit, spn_dag_env_t* env, spn_path_t object, spn_dag_obs_set_t* obs) {
  const spn_cc_toolchain_t* toolchain = &unit->target->pkg->build->toolchain->cc;

  spn_cc_depfile_t mode = spn_cc_depfile(toolchain, unit->lang);
  if (mode == SPN_CC_DEPFILE_NONE) {
    return spn_compile_object_run(unit, object, (spn_path_t) sp_zero) ? SPN_ERR_DAG_ACTION : SPN_OK;
  }

  spn_path_t depfile = spn_path_suffix(scratch, object, sp_str_lit(".d"));
  if (spn_compile_object_run(unit, object, depfile)) {
    return SPN_ERR_DAG_ACTION;
  }

  sp_str_t dep = spn_path_str(g->roots, scratch, depfile);
  if (!sp_fs_exists(dep)) {
    return mode == SPN_CC_DEPFILE_REQUIRED ? SPN_ERR_DAG_DEPFILE : SPN_OK;
  }
  sp_str_t content = sp_zero;
  sp_da(sp_str_t) prereqs = sp_zero;
  if (sp_io_read_file(scratch, dep, &content) || spn_cc_parse_depfile(scratch, toolchain, content, &prereqs)) {
    return SPN_ERR_DAG_DEPFILE;
  }
  sp_da_for(prereqs, it) {
    sp_str_t path = prereqs[it];
    if (!sp_fs_is_absolute(path)) {
      path = spn_path_str(g->roots, scratch, spn_path_join(scratch, unit->target->pkg->paths.work, path));
    }
    sp_str_t canonical = spn_dag_file_cache_canonical(env->files, path);
    if (sp_str_empty(canonical)) {
      canonical = spn_dag_wasi_canonicalize(scratch, path);
    }
    sp_assert(!sp_str_empty(canonical));
    spn_dag_observe(obs, (spn_dag_obs_t) {
      .kind = SPN_DAG_OBS_FILE,
      .path = spn_path_make(g->roots, canonical),
    });
  }
  return SPN_OK;
}

static spn_err_t on_compile_object(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = compile_object(s.mem, g, (spn_compile_unit_t*)user_data, env, outputs[0], obs);
  sp_mem_end_scratch(s);
  return err;
}


static spn_err_t on_link_target(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_dag_link_ctx_t* link = (spn_dag_link_ctx_t*)user_data;

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_da(spn_path_t) objects = sp_da_new(s.mem, spn_path_t);
  sp_da_for(link->objects, it) {
    sp_da_push(objects, dag_artifact_path(g, link->objects[it]));
  }
  spn_cc_link_files_t files = {
    .output = outputs[0],
    .objects = objects,
    .exports.path = link->exports.occupied ? dag_artifact_path(g, link->exports) : (spn_path_t) sp_zero,
    .implib = link->implib ? outputs[1] : (spn_path_t) sp_zero,
  };
  spn_err_t err = spn_link_target(link->target, files);
  sp_mem_end_scratch(s);
  return err ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static spn_err_t dag_exports_exec(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_dag_link_ctx_t* link = (spn_dag_link_ctx_t*)user_data;

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_da(spn_path_t) objects = sp_da_new(s.mem, spn_path_t);
  sp_da_for(link->objects, it) {
    sp_da_push(objects, dag_artifact_path(g, link->objects[it]));
  }
  spn_err_t err = spn_link_exports_run(link->target, objects, outputs[0]);
  sp_mem_end_scratch(s);
  return err ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static spn_err_t generate_embedding(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_target_unit_t* target = (spn_target_unit_t*)user_data;

  if (spn_embed_write(target, outputs[0], outputs[1], obs)) {
    return SPN_ERR_DAG_ACTION;
  }
  return SPN_OK;
}

static spn_err_t dag_user_exec(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_user_node_t* node = (spn_user_node_t*)user_data;
  spn_pkg_unit_t* pkg = node->pkg;

  spn_pkg_unit_announce_compile(pkg);

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SCRIPT_USER_FN,
    .pkg = pkg->info->name,
    .script_user_fn = { .tag = node->tag }
  });

  sp_da_for(action->produces, it) {
    spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    sp_str_t declared = spn_path_str(g->roots, scratch.mem, artifact->path);
    sp_err_t err = SP_OK;
    switch (artifact->kind) {
      case SPN_DAG_ARTIFACT_KIND_FILE:  err = sp_fs_remove_file(declared); break;
      case SPN_DAG_ARTIFACT_KIND_TREE:  err = sp_fs_remove_dir(declared); break;
      case SPN_DAG_ARTIFACT_KIND_VALUE: sp_unreachable_case();
    }
    sp_mem_end_scratch(scratch);
    if (err && err != SP_ERR_SYS_NOT_FOUND) {
      spn_event_buffer_push(spn.events, (spn_event_t) {
        .kind = SPN_EVENT_NODE_FAILED,
        .pkg = pkg->info->name,
        .node_failed = {
          .path = spn_path_str(g->roots, spn.mem, artifact->path),
          .message = sp_fmt(spn.mem, "could not be removed before node {} ran", sp_fmt_str(node->tag)).value,
        },
      });
      return SPN_ERR_DAG_ACTION;
    }
  }

  if (!sp_str_empty(node->fn)) {
    if (spn_wasm_call_export_ex(pkg, node->fn, SPN_ABI_KIND_NONE, SP_NULLPTR, obs)) {
      return SPN_ERR_DAG_ACTION;
    }
  }

  sp_da_for(action->produces, it) {
    spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    sp_str_t target = spn_path_str(g->roots, scratch.mem, outputs[it]);
    sp_str_t declared = spn_path_str(g->roots, scratch.mem, artifact->path);
    sp_err_t err = SP_OK;
    if (node->outputs[it].stamp) {
      sp_fs_create_file(target);
    }
    else {
      switch (artifact->kind) {
        case SPN_DAG_ARTIFACT_KIND_FILE:  err = sp_fs_copy_file(declared, target, SP_FS_ATOMIC_REPLACE); break;
        case SPN_DAG_ARTIFACT_KIND_TREE:  err = sp_fs_copy_tree(declared, target, SP_FS_ATOMIC_REPLACE); break;
        case SPN_DAG_ARTIFACT_KIND_VALUE: sp_unreachable_case();
      }
    }
    sp_mem_end_scratch(scratch);
    if (err) {
      spn_event_buffer_push(spn.events, (spn_event_t) {
        .kind = SPN_EVENT_NODE_FAILED,
        .pkg = pkg->info->name,
        .node_failed = {
          .path = spn_path_str(g->roots, spn.mem, artifact->path),
          .message = err == SP_ERR_SYS_NOT_FOUND
            ? sp_fmt(spn.mem, "was declared as an output of node {} but was not produced", sp_fmt_str(node->tag)).value
            : sp_fmt(spn.mem, "output of node {} could not be copied into the build", sp_fmt_str(node->tag)).value,
        },
      });
      return SPN_ERR_DAG_ACTION;
    }
  }

  return SPN_OK;
}

static spn_err_t publish_copy(sp_mem_t scratch, spn_tree_roots_t trees, sp_str_t root, spn_publish_copy_t* copy, spn_dag_obs_set_t* obs) {
  spn_path_t pattern = spn_path_join(scratch, spn_tree_root(trees, copy->tree), copy->pattern);
  spn_dag_glob_result_t glob = sp_zero;
  spn_try(spn_dag_glob(scratch, &spn.roots, pattern, &glob));
  sp_da_for(glob.obs, it) {
    spn_dag_observe(obs, glob.obs[it]);
  }
  if (sp_da_empty(glob.matches)) {
    return SPN_ERROR;
  }

  sp_str_t dir = sp_fs_join_path(scratch, root, copy->dest);
  sp_da_for(glob.matches, it) {
    spn_try(spn_fs_update_file(
      spn_path_str(&spn.roots, scratch, glob.matches[it].path),
      sp_fs_join_path(scratch, dir, glob.matches[it].rel)
    ));
  }
  return SPN_OK;
}

static spn_err_t publish_tree(sp_mem_t scratch, spn_dag_t* g, spn_pkg_unit_t* unit, spn_path_t include, spn_path_t stamp, spn_dag_obs_set_t* obs) {
  sp_str_t root = spn_path_str(g->roots, scratch, include);
  if (spn_pkg_unit_publish_headers(unit, root)) {
    return SPN_ERR_DAG_ACTION;
  }

  sp_da_for(unit->info->publish.copy, it) {
    spn_publish_copy_t* copy = &unit->info->publish.copy[it];
    if (publish_copy(scratch, unit->paths.roots, root, copy, obs)) {
      spn_event_buffer_push(spn.events, (spn_event_t) {
        .kind = SPN_EVENT_NODE_FAILED,
        .pkg = unit->info->name,
        .node_failed = {
          .path = sp_fs_join_path(spn.mem, spn_tree_to_str(copy->tree), copy->pattern),
          .message = sp_fmt(spn.mem, "could not be published to {}", sp_fmt_str(sp_fs_join_path(spn.mem, sp_str_lit("include"), copy->dest))).value,
        },
      });
      return SPN_ERR_DAG_ACTION;
    }
  }

  sp_fs_create_file(spn_path_str(g->roots, scratch, stamp));
  return SPN_OK;
}

static spn_err_t dag_tree_exec(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = publish_tree(s.mem, g, (spn_pkg_unit_t*)user_data, outputs[0], outputs[1], obs);
  sp_mem_end_scratch(s);
  return err;
}

static spn_err_t dag_compile_commands_exec(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_pkg_unit_t* unit = (spn_pkg_unit_t*)user_data;

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = spn_pkg_unit_write_compile_commands(g->roots, unit, spn_path_str(g->roots, s.mem, outputs[0]));
  sp_mem_end_scratch(s);
  return err ? SPN_ERR_DAG_OUTPUT_WRITE : SPN_OK;
}

static spn_err_t dag_compile_commands_merge_exec(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_da(sp_str_t) fragments = sp_da_new(s.mem, sp_str_t);
  sp_da_for(action->consumes, it) {
    sp_da_push(fragments, spn_path_str(g->roots, s.mem, dag_artifact_path(g, action->consumes[it])));
  }
  spn_err_t err = spn_compile_commands_merge(fragments, spn_path_str(g->roots, s.mem, outputs[0]));
  if (!err) {
    sp_fs_create_file(spn_path_str(g->roots, s.mem, outputs[1]));
  }
  sp_mem_end_scratch(s);
  return err ? SPN_ERR_DAG_OUTPUT_WRITE : SPN_OK;
}

//////////////////
// CONSTRUCTION //
//////////////////
static spn_err_t dag_add_user_nodes(spn_dag_build_t* b, spn_pkg_unit_t* unit, sp_da(spn_dag_id_t)* outputs) {
  spn_dag_t* g = b->graph;
  spn_build_source_pin_t pin = spn_build_source_pin(unit);

  spn_dag_id_t configure = sp_zero;
  if (unit->metaprogram && unit->metaprogram->scripts.configure) {
    configure = spn_dag_add_file(g, spn_target_output_path(b->mem, unit->metaprogram->scripts.configure));
  }
  spn_dag_id_t metaprogram = sp_zero;
  if (unit->metaprogram && unit->metaprogram->scripts.build) {
    spn_dag_target_ids_t* ids = sp_ht_getp(b->ids.targets, unit->metaprogram->scripts.build);
    if (ids) {
      metaprogram = ids->output;
    }
  }

  sp_da_for(unit->user_nodes, it) {
    spn_user_node_t* node = &unit->user_nodes[it];
    if (sp_da_empty(node->outputs)) {
      sp_da_push(node->outputs, spn_pkg_unit_node_stamp(unit, node));
    }
  }

  sp_da_for(unit->user_nodes, it) {
    spn_user_node_t* node = &unit->user_nodes[it];

    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .kind = SPN_DAG_ACTION_DISCOVERED,
      .identity = spn_build_user_identity(node, &pin),
      .execute = dag_user_exec,
      .user_data = node,
    });

    if (metaprogram.occupied) {
      spn_dag_action_add_input(g, action, metaprogram);
    }
    if (configure.occupied) {
      spn_dag_action_add_input(g, action, configure);
    }
    sp_da_for(node->inputs, jt) {
      spn_dag_action_add_input(g, action, spn_dag_add_file(g, node->inputs[jt]));
    }
    sp_da_for(node->deps, jt) {
      spn_user_node_t* dep = spn_node_deref(node->deps[jt]);
      sp_da_for(dep->outputs, ot) {
        spn_dag_action_add_input(g, action, spn_dag_add_path(g, dep->outputs[ot].path, dep->outputs[ot].kind));
      }
    }

    sp_da_for(node->outputs, ot) {
      spn_user_output_t* out = &node->outputs[ot];
      spn_dag_id_t artifact = spn_dag_add_path(g, out->path, out->kind);
      spn_err_t err = spn_dag_action_add_output(g, action, artifact);
      if (err) {
        b->env.diag = (spn_dag_diag_t) {
          .err = err,
          .path = spn_path_str(g->roots, b->mem, out->path)
        };
        return err;
      }
      sp_da_push(*outputs, artifact);
    }
  }

  return SPN_OK;
}

static spn_err_t add_object_compilation(spn_dag_build_t* b, spn_target_unit_t* target) {
  spn_dag_t* g = b->graph;

  sp_da_for(target->objects, it) {
    spn_compile_unit_t* unit = target->objects[it];
    bool exists = sp_ht_getp(b->ids.objects, unit);
    sp_assert(!exists);

    spn_dag_action_config_t config = {
      .kind = SPN_DAG_ACTION_DISCOVERED,
      .identity = spn_build_compile_identity(unit),
      .execute = on_compile_object,
      .user_data = unit,
    };

    spn_dag_object_ids_t ids = sp_zero;
    ids.action = spn_dag_add_action(g, config);
    spn_dag_action_add_input(g, ids.action, spn_dag_add_file(g, unit->paths.file));

    ids.object = spn_dag_add_file(g, unit->paths.object);
    spn_try(spn_dag_action_add_output(g, ids.action, ids.object));

    sp_ht_insert(b->ids.objects, unit, ids);
  }

  return SPN_OK;
}

static spn_path_t embed_artifact_path(sp_mem_t mem, spn_target_unit_t* unit, const c8* extension) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  sp_str_t name = sp_fmt(s.mem, "{}.embed.{}", sp_fmt_str(unit->info->name), sp_fmt_cstr(extension)).value;
  spn_path_t path = spn_path_join(mem, spn_target_unit_object_dir(s.mem, unit), name);
  sp_mem_end_scratch(s);
  return path;
}

static spn_err_t dag_add_exports(spn_dag_build_t* b, spn_dag_link_ctx_t* link) {
  spn_dag_t* g = b->graph;
  spn_target_unit_t* target = link->target;

  spn_path_t output = spn_target_exports_path(b->mem, target);
  spn_dag_digest_t identity = sp_zero;
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = spn_build_exports_identity(s.mem, target, output, dag_declared_paths(s.mem, g, link->objects), &identity);
  sp_mem_end_scratch(s);
  spn_try(err);

  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = identity,
    .execute = dag_exports_exec,
    .user_data = link,
  });
  link->exports = spn_dag_add_file(g, output);
  spn_try(spn_dag_action_add_output(g, action, link->exports));

  sp_da_for(link->objects, it) {
    spn_dag_action_add_input(g, action, link->objects[it]);
  }
  sp_da_for(target->link.archives, it) {
    spn_dag_action_add_input(g, action, spn_dag_add_file(g, target->link.archives[it]));
  }
  return SPN_OK;
}

spn_err_t spn_dag_build_add_target(spn_dag_build_t* b, spn_target_unit_t* target) {
  spn_dag_t* g = b->graph;

  switch (target->lib_kind) {
    case SPN_LIB_KIND_SOURCE: {
      return SPN_OK;
    }
    case SPN_LIB_KIND_OBJECT: {
      spn_try(add_object_compilation(b, target));
      return SPN_OK;
    }
    case SPN_LIB_KIND_STATIC:
    case SPN_LIB_KIND_SHARED:
    case SPN_LIB_KIND_NONE: {
      break;
    }
  }

  bool exists = sp_ht_getp(b->ids.targets, target);
  sp_assert(!exists);

  spn_try(add_object_compilation(b, target));

  if (sp_da_empty(target->objects)) {
    return SPN_OK;
  }

  spn_dag_target_ids_t ids = sp_zero;

  if (!sp_da_empty(target->info->embed)) {
    ids.embed.action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .kind = SPN_DAG_ACTION_DISCOVERED,
      .identity = hash_embedding(target),
      .execute = generate_embedding,
      .user_data = target,
    });
    ids.embed.object = spn_dag_add_file(g, embed_artifact_path(b->mem, target, "o"));
    ids.embed.header = spn_dag_add_file(g, embed_artifact_path(b->mem, target, "h"));
    spn_try(spn_dag_action_add_output(g, ids.embed.action, ids.embed.object));
    spn_try(spn_dag_action_add_output(g, ids.embed.action, ids.embed.header));

    sp_da_for(target->info->embed, it) {
      spn_embed_t* entry = &target->info->embed[it];
      if (entry->kind == SPN_EMBED_FILE) {
        spn_dag_action_add_input(g, ids.embed.action, spn_dag_add_file(g, entry->path));
      }
    }

    sp_da_for(target->objects, it) {
      spn_dag_object_ids_t* object = sp_ht_getp(b->ids.objects, target->objects[it]);
      sp_assert(object);
      spn_dag_action_add_input(g, object->action, ids.embed.header);
    }
  }

  spn_dag_link_ctx_t* link = sp_alloc_type(b->mem, spn_dag_link_ctx_t);
  link->target = target;
  sp_da_init(b->mem, link->objects);
  sp_da_for(target->objects, it) {
    spn_dag_object_ids_t* object = sp_ht_getp(b->ids.objects, target->objects[it]);
    sp_assert(object);
    sp_da_push(link->objects, object->object);
  }
  if (ids.embed.object.occupied) {
    sp_da_push(link->objects, ids.embed.object);
  }

  if (target->kind == SPN_CC_OUTPUT_SHARED_LIB || target->kind == SPN_CC_OUTPUT_REACTOR) {
    spn_try(dag_add_exports(b, link));
  }

  spn_triple_t triple = spn_profile_triple(&target->pkg->build->profile);
  link->implib = target->kind == SPN_CC_OUTPUT_SHARED_LIB && spn_ld_dialect(triple) == SPN_LD_DIALECT_LINK;
  spn_cc_link_files_t files = {
    .output = spn_target_output_path(b->mem, target),
    .exports.path = link->exports.occupied ? dag_artifact_declared(g, link->exports) : (spn_path_t) sp_zero,
  };
  if (link->implib) {
    sp_mem_arena_marker_t s = sp_mem_begin_scratch();
    sp_str_t file_name = spn_triple_lib_file_name(s.mem, triple, target->info->name, SP_OS_LIB_STATIC);
    files.implib = spn_path_join(b->mem, target->pkg->paths.lib, file_name);
    sp_mem_end_scratch(s);
  }

  spn_dag_digest_t identity = sp_zero;
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  files.objects = dag_declared_paths(s.mem, g, link->objects);
  spn_err_t err = spn_build_link_identity(s.mem, target, &files, &identity);
  sp_mem_end_scratch(s);
  spn_try(err);

  ids.action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = identity,
    .execute = on_link_target,
    .user_data = link,
  });
  sp_da_for(link->objects, it) {
    spn_dag_action_add_input(g, ids.action, link->objects[it]);
  }
  sp_da_for(target->link.cc.scripts, it) {
    spn_dag_action_add_input(g, ids.action, spn_dag_add_file(g, target->link.cc.scripts[it]));
  }
  if (link->exports.occupied) {
    spn_dag_action_add_input(g, ids.action, link->exports);
  }
  ids.output = spn_dag_add_file(g, files.output);
  spn_try(spn_dag_action_add_output(g, ids.action, ids.output));
  if (link->implib) {
    spn_try(spn_dag_action_add_output(g, ids.action, spn_dag_add_file(g, files.implib)));
  }

  sp_ht_insert(b->ids.targets, target, ids);
  return SPN_OK;
}

static bool dag_pkg_publishes(spn_pkg_unit_t* unit) {
  spn_pkg_unit_header_maps_t published = spn_pkg_unit_header_maps(unit);
  sp_for(mt, published.count) {
    sp_om_for(published.maps[mt], it) {
      if (!sp_da_empty(sp_str_om_at(published.maps[mt], it)->headers)) {
        return true;
      }
    }
  }

  return !sp_da_empty(unit->info->publish.copy);
}

static spn_err_t dag_add_tree(spn_dag_build_t* b, spn_pkg_unit_t* unit) {
  spn_dag_t* g = b->graph;

  if (!dag_pkg_publishes(unit)) {
    return SPN_OK;
  }

  spn_build_source_pin_t pin = spn_build_source_pin(unit);
  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .kind = SPN_DAG_ACTION_DISCOVERED,
    .identity = spn_build_tree_identity(unit, &pin),
    .execute = dag_tree_exec,
    .user_data = unit,
  });
  spn_try(spn_dag_action_add_output(g, action, spn_dag_add_tree(g, unit->paths.include)));
  spn_dag_id_t stamp = spn_dag_add_output(g, sp_str_lit("include.stamp"));
  spn_try(spn_dag_action_add_output(g, action, stamp));
  sp_ht_insert(b->ids.stamps, unit->paths.include, stamp);

  spn_pkg_unit_header_maps_t published = spn_pkg_unit_header_maps(unit);
  sp_for(mt, published.count) {
    sp_om_for(published.maps[mt], it) {
      spn_target_info_t* target = sp_str_om_at(published.maps[mt], it);
      sp_da_for(target->headers, ht) {
        spn_dag_action_add_input(g, action, spn_dag_add_file(g, target->headers[ht]));
      }
    }
  }

  return SPN_OK;
}

static spn_err_t dag_add_package(spn_dag_build_t* b, spn_pkg_unit_t* unit) {
  sp_da(spn_dag_id_t) user_outputs = sp_da_new(b->mem, spn_dag_id_t);
  spn_try(dag_add_user_nodes(b, unit, &user_outputs));
  spn_try(dag_add_tree(b, unit));
  sp_ht_insert(b->ids.user_outputs, unit, user_outputs);

  return SPN_OK;
}

static void dag_add_link_deps(spn_dag_build_t* b, spn_target_unit_t* target, spn_dag_id_t action) {
  spn_dag_t* g = b->graph;

  sp_da_for(target->link.libs, it) {
    spn_dag_target_ids_t* dep = sp_ht_getp(b->ids.targets, target->link.libs[it].lib);
    if (dep) {
      spn_dag_action_add_input(g, action, dep->output);
    }
  }
}

static void dag_add_target_edges(spn_dag_build_t* b, spn_target_unit_t* target) {
  spn_dag_t* g = b->graph;
  spn_pkg_unit_t* unit = target->pkg;

  if (target->lib_kind == SPN_LIB_KIND_SOURCE) {
    return;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();

  sp_da(spn_dag_id_t) inputs = sp_da_new(s.mem, spn_dag_id_t);
  sp_da(spn_dag_id_t)* user_outputs = sp_ht_getp(b->ids.user_outputs, unit);
  if (user_outputs) {
    sp_da_for(*user_outputs, it) {
      sp_da_push(inputs, (*user_outputs)[it]);
    }
  }
  sp_da_for(target->include, it) {
    spn_dag_id_t* stamp = sp_ht_getp(b->ids.stamps, target->include[it]);
    if (stamp) {
      sp_da_push(inputs, *stamp);
    }
  }

  sp_da_for(target->objects, ot) {
    spn_dag_object_ids_t* object = sp_ht_getp(b->ids.objects, target->objects[ot]);
    if (!object) {
      continue;
    }
    sp_da_for(inputs, it) {
      spn_dag_action_add_input(g, object->action, inputs[it]);
    }
  }

  spn_dag_target_ids_t* found = sp_ht_getp(b->ids.targets, target);
  spn_dag_target_ids_t target_ids = found ? *found : (spn_dag_target_ids_t) sp_zero;

  if (target_ids.action.occupied) {
    dag_add_link_deps(b, target, target_ids.action);
  }

  if (target_ids.embed.action.occupied) {
    sp_da_for(target->info->embed, et) {
      spn_embed_t* embed = &target->info->embed[et];
      if (embed->kind != SPN_EMBED_DIR) {
        continue;
      }
      sp_da_for(g->artifacts, at) {
        spn_dag_artifact_t* artifact = &g->artifacts[at];
        if (spn_dag_output_overlaps(artifact, embed->path)) {
          spn_dag_action_add_input(g, target_ids.embed.action, artifact->id);
        }
      }
    }
  }

  sp_mem_end_scratch(s);
}

static spn_err_t dag_add_unit_targets(spn_dag_build_t* b, sp_da(spn_pkg_unit_t*) units) {
  sp_da_for(units, it) {
    sp_da_for(units[it]->targets, jt) {
      spn_try(spn_dag_build_add_target(b, units[it]->targets[jt]));
    }
  }
  return SPN_OK;
}

static void dag_add_unit_target_edges(spn_dag_build_t* b, sp_da(spn_pkg_unit_t*) units) {
  sp_da_for(units, it) {
    sp_da_for(units[it]->targets, jt) {
      dag_add_target_edges(b, units[it]->targets[jt]);
    }
  }
}

static spn_err_t dag_add_compile_commands(spn_dag_build_t* b) {
  spn_dag_t* g = b->graph;
  spn_session_t* session = b->session;

  sp_str_t tag = sp_str_lit("spn.build.compile_commands.merge.v1");
  spn_dag_id_t merge = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = spn_dag_digest(tag.data, tag.len),
    .execute = dag_compile_commands_merge_exec,
  });
  b->compile_commands = spn_dag_add_output(g, sp_str_lit("compile_commands.json"));
  spn_dag_id_t stamp = spn_dag_add_output(g, sp_str_lit("compile_commands.stamp"));
  spn_try(spn_dag_action_add_output(g, merge, b->compile_commands));
  spn_try(spn_dag_action_add_output(g, merge, stamp));

  sp_om_for(session->units.packages, it) {
    spn_pkg_unit_t* unit = sp_om_at(session->units.packages, it);

    sp_mem_arena_marker_t s = sp_mem_begin_scratch();
    sp_da(spn_compile_unit_t*) objects = spn_pkg_unit_objects(s.mem, unit);
    if (sp_da_empty(objects)) {
      sp_mem_end_scratch(s);
      continue;
    }
    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .identity = spn_build_compile_commands_identity(g->roots, unit, objects),
      .execute = dag_compile_commands_exec,
      .user_data = unit,
    });
    sp_mem_end_scratch(s);

    spn_dag_id_t fragment = spn_dag_add_file(g, spn_path_join(b->mem, unit->paths.work, sp_str_lit("compile_commands.json")));
    spn_try(spn_dag_action_add_output(g, action, fragment));
    spn_dag_action_add_input(g, merge, fragment);
  }

  sp_ht_for_kv(b->ids.objects, it) {
    spn_dag_action_add_input(g, it.val->action, stamp);
  }

  return SPN_OK;
}

static spn_err_t prepare_graph(spn_dag_build_t* b) {
  spn_session_t* session = b->session;

  sp_om_for(session->units.builds, it) {
    spn_build_unit_t* build = sp_om_at(session->units.builds, it);
    spn_try(dag_add_unit_targets(b, build->packages));
  }

  sp_om_for(session->units.builds, it) {
    spn_build_unit_t* build = sp_om_at(session->units.builds, it);
    sp_da_for(build->packages, jt) {
      if (spn_pkg_unit_is_script_host(build->packages[jt])) {
        continue;
      }
      spn_try(dag_add_package(b, build->packages[jt]));
    }
  }

  sp_om_for(session->units.builds, it) {
    spn_build_unit_t* build = sp_om_at(session->units.builds, it);
    dag_add_unit_target_edges(b, build->packages);
  }

  spn_try(dag_add_compile_commands(b));

  return SPN_OK;
}

/////////
// RUN //
/////////
static spn_err_t dag_stage_copy(spn_dag_build_t* b, spn_dag_id_t id, spn_path_t to) {
  spn_dag_artifact_t* artifact = spn_dag_find_artifact(b->graph, id);

  sp_sys_file_meta_t staged_meta = sp_zero;
  spn_dag_digest_t staged_digest = sp_zero;
  if (!spn_dag_file_cache_stat(b->env.files, to, &staged_meta) && staged_meta.nlink == 1 &&
      !spn_dag_file_cache_digest(b->env.files, to, &staged_digest) &&
      spn_dag_digest_equal(staged_digest, artifact->digest)) {
    return SPN_OK;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t source = spn_path_str(b->graph->roots, scratch.mem, artifact->materialized);
  sp_str_t target = spn_path_str(b->graph->roots, scratch.mem, to);
  sp_fs_create_dir(sp_fs_parent_path(target));
  sp_err_t copied = sp_fs_copy_file(source, target, SP_FS_ATOMIC_REPLACE);
  sp_mem_end_scratch(scratch);

  spn_err_t err = copied ? SPN_ERR_DAG_OUTPUT_WRITE : spn_dag_file_cache_seed(b->env.files, to, artifact->digest);
  if (err) {
    spn_dag_file_cache_invalidate(b->env.files, to);
    return spn_err_emit(b->session->ctx, (spn_err_union_t) {
      .kind = err,
      .dag = { .path = spn_path_str(b->graph->roots, b->mem, to) },
    });
  }
  return SPN_OK;
}

typedef struct {
  sp_str_t exe;
  sp_str_t entry;
} dag_staged_t;

static spn_err_t dag_stage(spn_dag_build_t* b) {
  spn_session_t* session = b->session;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = SPN_OK;

  sp_da_for(session->plans, it) {
    spn_build_plan_t* plan = &session->plans[it];
    spn_path_t root = plan->build->paths.root;
    sp_str_t manifest = spn_path_str(b->graph->roots, scratch.mem, spn_path_join(scratch.mem, root, sp_str_lit(".spn/staged")));

    sp_str_ht(bool) exes = SP_NULLPTR;
    sp_str_ht_init(scratch.mem, exes);
    sp_da(dag_staged_t) next = sp_da_new(scratch.mem, dag_staged_t);
    sp_da_for(plan->staged, jt) {
      spn_stage_closure_t* closure = &plan->staged[jt];
      sp_str_ht_insert(exes, closure->exe.path.sub, true);
      sp_da_push(next, ((dag_staged_t) { .exe = closure->exe.path.sub, .entry = closure->exe.path.sub }));
      sp_da_for(closure->libs, lt) {
        sp_da_push(next, ((dag_staged_t) { .exe = closure->exe.path.sub, .entry = closure->libs[lt].path.sub }));
      }
    }

    sp_str_t content = sp_zero;
    sp_io_read_file(scratch.mem, manifest, &content);
    sp_da(sp_str_t) lines = sp_str_split_c8(scratch.mem, content, '\n');
    sp_da(dag_staged_t) previous = sp_da_new(scratch.mem, dag_staged_t);
    sp_da_for(lines, jt) {
      s32 tab = sp_str_find_c8(lines[jt], '\t');
      if (tab == SP_STR_NO_MATCH) {
        continue;
      }
      dag_staged_t staged = { .exe = sp_str_prefix(lines[jt], tab), .entry = sp_str_suffix(lines[jt], lines[jt].len - tab - 1) };
      sp_da_push(previous, staged);
      if (!sp_str_ht_get(exes, staged.exe)) {
        sp_da_push(next, staged);
      }
    }

    sp_str_ht(bool) live = SP_NULLPTR;
    sp_str_ht_init(scratch.mem, live);
    sp_io_dyn_mem_writer_t sink = sp_zero;
    sp_io_dyn_mem_writer_init(scratch.mem, &sink);
    sp_da_for(next, jt) {
      sp_str_ht_insert(live, next[jt].entry, true);
      sp_fmt_io(&sink.base, "{}\t{}\n", sp_fmt_str(next[jt].exe), sp_fmt_str(next[jt].entry));
    }
    sp_da_for(previous, jt) {
      if (sp_str_ht_get(live, previous[jt].entry)) {
        continue;
      }
      spn_path_t path = { .root = root.root, .sub = previous[jt].entry };
      sp_fs_remove_file(spn_path_str(b->graph->roots, scratch.mem, path));
      spn_dag_file_cache_invalidate(b->env.files, path);
    }
    sp_fs_create_dir(sp_fs_parent_path(manifest));
    sp_fs_write_atomic(manifest, sp_io_dyn_mem_writer_as_str(&sink));

    sp_str_ht(bool) copied = SP_NULLPTR;
    sp_str_ht_init(scratch.mem, copied);
    sp_da_for(plan->staged, jt) {
      spn_stage_closure_t* closure = &plan->staged[jt];
      spn_dag_target_ids_t* ids = sp_ht_getp(b->ids.targets, closure->exe.target);
      if (!ids) {
        continue;
      }
      err = dag_stage_copy(b, ids->output, closure->exe.path);
      if (err) {
        goto done;
      }
      sp_da_for(closure->libs, lt) {
        spn_stage_entry_t* lib = &closure->libs[lt];
        spn_dag_target_ids_t* lib_ids = sp_ht_getp(b->ids.targets, lib->target);
        if (!lib_ids || sp_str_ht_get(copied, lib->path.sub)) {
          continue;
        }
        sp_str_ht_insert(copied, lib->path.sub, true);
        err = dag_stage_copy(b, lib_ids->output, lib->path);
        if (err) {
          goto done;
        }
      }
    }
  }

done:
  sp_mem_end_scratch(scratch);
  return err;
}

static spn_err_t dag_result(spn_dag_build_t* b) {
  spn_dag_diag_t* diag = &b->env.diag;

  switch (b->result) {
    case SPN_OK: {
      return SPN_OK;
    }
    case SPN_ERR_DAG_CANCELLED:
    case SPN_ERR_DAG_ACTION: {
      return b->result;
    }
    default: {
      break;
    }
  }

  sp_str_t path = diag->path;
  if (sp_str_empty(path) && diag->action.occupied) {
    spn_dag_action_t* action = spn_dag_find_action(b->graph, diag->action);
    if (!sp_da_empty(action->produces)) {
      spn_dag_artifact_t* artifact = spn_dag_find_artifact(b->graph, action->produces[0]);
      path = spn_path_empty(artifact->path)
        ? artifact->name
        : spn_path_str(b->graph->roots, b->mem, artifact->path);
    }
  }

  return spn_err_emit(b->session->ctx, (spn_err_union_t) {
    .kind = diag->err ? diag->err : b->result,
    .dag = { .path = path },
  });
}

static void dag_emit_reports(spn_dag_build_t* b, u64 elapsed) {
  spn_session_t* session = b->session;
  bool failed = b->result != SPN_OK;
  u32 hits = (u32)sp_atomic_s32_load(&b->progress.hits, SP_ATOMIC_SEQ_CST);
  u32 misses = (u32)sp_atomic_s32_load(&b->progress.misses, SP_ATOMIC_SEQ_CST);

  sp_da_for(session->plans, it) {
    spn_build_unit_t* build = session->plans[it].build;
    spn_pkg_unit_t* root = spn_session_find_pkg_unit(session, build, spn_session_root_pkg(session));
    spn_pkg_info_t* pkg = root ? root->info : session->pkg;
    spn_profile_info_t* profile = &build->profile;

    if (failed) {
      spn_event_buffer_push(session->ctx->events, (spn_event_t) {
        .kind = SPN_EVENT_BUILD_FAILED,
        .pkg = pkg->name,
        .build_failed = {
          .profile = profile->name,
          .time = elapsed,
        },
      });
    }
    else {
      spn_event_buffer_push(session->ctx->events, (spn_event_t) {
        .kind = SPN_EVENT_BUILD_PASSED,
        .pkg = pkg->name,
        .build_passed = {
          .profile = profile->name,
          .time = elapsed,
          .hits = hits,
          .misses = misses,
        },
      });
    }

    spn_event_buffer_push(session->ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_BUILD_SUMMARY,
      .pkg = pkg->name,
      .build_summary = {
        .success = !failed,
        .hits = hits,
        .misses = misses,
        .total = (u32)sp_da_size(b->graph->actions),
        .time = elapsed,
        .profile = profile->name,
        .hashed_files = sp_atomic_u32_load(&b->stats.hashed_files, SP_ATOMIC_SEQ_CST),
        .hashed_bytes = sp_atomic_u64_load(&b->stats.hashed_bytes, SP_ATOMIC_SEQ_CST),
        .stats = sp_atomic_u32_load(&b->stats.stats, SP_ATOMIC_SEQ_CST),
        .obs_rows = sp_atomic_u32_load(&b->stats.obs_rows, SP_ATOMIC_SEQ_CST),
        .cache_reads = sp_atomic_u32_load(&b->stats.cache_reads, SP_ATOMIC_SEQ_CST),
        .cache_writes = sp_atomic_u32_load(&b->stats.cache_writes, SP_ATOMIC_SEQ_CST),
      },
    });
  }
}

spn_dag_build_t* spn_dag_build_new(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_dag_build_t* b = sp_alloc_type(session->mem, spn_dag_build_t);
  sp_mem_zero(b, sizeof(spn_dag_build_t));
  b->session = session;
  b->mem = spn.mem;
  b->graph = spn_dag_new(spn.mem, &spn.roots);
  sp_ht_init(b->mem, b->ids.user_outputs);
  sp_ht_init(b->mem, b->ids.stamps);
  sp_ht_set_fns(b->ids.stamps, spn_path_on_hash, spn_path_on_compare);
  sp_ht_init(b->mem, b->ids.targets);
  sp_ht_init(b->mem, b->ids.objects);

  spn_path_t root = spn_path_anchor(session->mem, &spn.roots, spn_path_join(session->mem, spn_path_from_root(SPN_PATH_ROOT_CACHE), sp_str_lit("dag")));
  spn_path_t tmp = spn_path_join(session->mem, root, sp_str_lit("tmp"));
  sp_str_t dir = spn_path_str(&spn.roots, session->mem, root);
  sp_fs_create_dir(dir);
  sp_fs_create_dir(spn_path_str(&spn.roots, session->mem, tmp));

  spn_dag_store_init(&b->store, (spn_dag_store_config_t) {
    .kind = SPN_DAG_STORE_FILESYSTEM,
    .mem = spn.mem,
    .roots = &spn.roots,
    .dir = spn_path_join(session->mem, root, sp_str_lit("store")),
  });
  spn_dag_action_cache_init(&b->actions, spn.mem, sp_fs_join_path(session->mem, dir, sp_str_lit("strong")));
  spn_dag_obs_table_init(&b->discovery, spn.mem, &spn.roots, sp_fs_join_path(session->mem, dir, sp_str_lit("weak")));
  session->dag.files.stats = &b->stats;
  b->actions.stats = &b->stats;
  b->discovery.stats = &b->stats;
  b->store.stats = &b->stats;

  b->env = (spn_dag_env_t) {
    .files = &session->dag.files,
    .cache = &b->actions,
    .store = &b->store,
    .discovery = &b->discovery,
    .stats = &b->stats,
    .progress = &b->progress,
    .wake = &op->ctx->wake,
    .cancel = &op->cancelled,
    .scratch = tmp,
  };

  return b;
}

spn_err_t spn_dag_build_run(spn_dag_build_t* b, u32 workers) {
  spn_thread_pool_init(&b->pool, spn.mem, (spn_thread_pool_config_t) {
    .workers = sp_min(workers, (u32)sp_da_size(b->graph->actions)),
    .on_worker_exit = spn_wasm_thread_exit,
  });

  b->timer = sp_tm_start_timer();
  spn_dag_file_cache_invalidate_all(b->env.files);
  b->result = spn_dag_run_executor(b->graph, &b->env, &b->pool.executor);
  spn_thread_pool_deinit(&b->pool);
  spn_dag_file_cache_flush(b->env.files, b->session->dag.files_path);
  return dag_result(b);
}

spn_err_t spn_dag_build_session(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_project_t* project = session->project;

  spn_dag_build_t* b = spn_dag_build_new(op);
  session->dag.build = b;

  spn_err_t prepared = prepare_graph(b);
  if (prepared) {
    b->result = prepared;
    return dag_result(b);
  }

  spn_triple_t target = { session->profile.arch, session->profile.os, session->profile.abi };
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_INIT_BUILD_GRAPH,
    .pkg = session->pkg->name,
    .graph_init = {
      .profile = session->profile.name,
      .target = spn_triple_to_str(session->mem, target),
      .toolchain = session->units.target->toolchain->info->name,
      .version = session->units.target->toolchain->version,
      .force = session->config.force,
    }
  });

  sp_atomic_ptr_store(&session->ctx->progress, &b->progress, SP_ATOMIC_SEQ_CST);
  spn_err_t result = spn_dag_build_run(b, spn_cpu_count());
  sp_atomic_ptr_store(&session->ctx->progress, SP_NULLPTR, SP_ATOMIC_SEQ_CST);
  u64 elapsed = sp_tm_read_timer(&b->timer);

  if (b->result == SPN_ERR_DAG_CANCELLED) {
    return result;
  }

  if (spn_dag_digest_valid(spn_dag_find_artifact(b->graph, b->compile_commands)->digest)) {
    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    spn_err_t staged = dag_stage_copy(b, b->compile_commands, spn_path_join(scratch.mem, session->paths.root, sp_str_lit("compile_commands.json")));
    sp_mem_end_scratch(scratch);
    if (!result) {
      result = staged;
    }
  }
  if (!result) {
    if (!project->lock.some) {
      spn_try(spn_project_update_lock(session->ctx, project, session->resolve));
    }
    result = dag_stage(b);
    spn_dag_file_cache_flush(b->env.files, session->dag.files_path);
  }
  if (!b->result) {
    b->result = result;
  }

  dag_emit_reports(b, elapsed);

  return result;
}
