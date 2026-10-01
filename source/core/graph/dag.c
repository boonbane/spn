#include "compiler/types.h"
#include "sp.h"
#include "io/io.h"
#include "fs/fs.h"
#include "macro/macro.h"
#include "project/project.h"
#include "ctx/types.h"
#include "error/error.h"
#include "spn/errors.h"
#include "event/types.h"
#include "core/types.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "compiler/rsp.h"
#include "core/core.h"
#include "cpu/cpu.h"
#include "enum/enum.h"
#include "event/event.h"
#include "external/wasm/wasm.h"
#include "op/op.h"
#include "paths/paths.h"
#include "stage/stage.h"
#include "str/str.h"
#include "session/session.h"
#include "thread_pool/thread_pool.h"
#include "unit/unit.h"
#include "graph/build.h"
#include "graph/dag.h"
#include "graph/nodes/nodes.h"
#include "triple/triple.h"
#include "unit/package.h"

static spn_path_t dag_artifact_declared(spn_dag_t* g, spn_dag_id_t id) {
  return spn_dag_find_artifact(g, id)->path;
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

typedef struct {
  spn_pkg_root_kind_t kind;
  sp_str_t rev;
  sp_str_t dir;
  sp_hash_t patches;
} source_pin_t;

static source_pin_t source_pin(spn_pkg_unit_t* unit) {
  source_pin_t pin = sp_zero;
  spn_resolved_pkg_t* resolved = sp_ht_getp(unit->session->resolve, unit->id.pkg);
  if (!resolved || resolved->origin.source.kind != SPN_PKG_ROOT_GIT) {
    return pin;
  }
  pin.kind = resolved->origin.source.kind;
  pin.rev = resolved->origin.source.git.rev;
  pin.dir = resolved->origin.source.git.dir;
  pin.patches = resolved->origin.source.git.patches.hash;
  return pin;
}

static void hash_pin(spn_digest_ctx_t* ctx, const source_pin_t* pin) {
  spn_dag_hash_u8(ctx, (u8)pin->kind);
  spn_dag_hash_str(ctx, pin->rev);
  spn_dag_hash_str(ctx, pin->dir);
  spn_dag_hash_u64(ctx, pin->patches);
}

//////////////////
// CONSTRUCTION //
//////////////////
static spn_err_t dag_add_user_nodes(spn_dag_build_t* b, spn_pkg_unit_t* unit, sp_da(spn_dag_id_t)* outputs) {
  spn_dag_t* g = b->graph;
  source_pin_t pin = source_pin(unit);

  spn_dag_id_t configure = sp_zero;
  if (unit->metaprogram && unit->metaprogram->scripts.configure) {
    configure = spn_dag_add_file(g, unit->metaprogram->scripts.configure->paths.output);
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

    spn_digest_ctx_t digest = sp_zero;
    spn_digest_init_blake3(&digest);
    spn_dag_hash_str(&digest, sp_str_lit("spn.build.user.v8"));
    spn_dag_hash_str(&digest, node->pkg->info->qualified);
    spn_dag_hash_u64(&digest, node->pkg->fingerprint);
    hash_pin(&digest, &pin);
    spn_dag_hash_str(&digest, node->tag);
    spn_dag_hash_str(&digest, node->fn);
    spn_dag_hash_paths(&digest, node->inputs);
    spn_dag_hash_u64(&digest, sp_da_size(node->outputs));
    sp_da_for(node->outputs, ot) {
      spn_dag_hash_u64(&digest, node->outputs[ot].dir);
      spn_dag_hash_str(&digest, node->outputs[ot].sub);
      spn_dag_hash_u64(&digest, node->outputs[ot].kind);
    }

    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .kind = SPN_DAG_ACTION_DISCOVERED,
      .identity = spn_dag_hash_final(&digest),
      .execute = spn_dag_exec_user,
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
  spn_session_t* session = b->session;
  spn_toolchain_unit_t* toolchain = target->pkg->build->toolchain;

  sp_da_for(target->objects, it) {
    spn_compile_unit_t* unit = target->objects[it];
    spn_invocation_t* invocation = spn_session_get_object_plan(session, unit->id);

    spn_dag_digest_t* identity = SP_NULLPTR;
    sp_om_emplace(session->dag.objects, unit->id, identity);

    spn_digest_ctx_t digest = sp_zero;
    spn_digest_init_blake3(&digest);
    spn_dag_hash_str(&digest, sp_str_lit("spn.build.compile.v6"));
    spn_dag_hash_u64(&digest, toolchain->identity);
    spn_dag_hash_arg(&digest, invocation->program);
    spn_dag_hash_path(&digest, invocation->cwd);
    spn_dag_hash_args(&digest, invocation->args);
    spn_dag_hash_u64(&digest, sp_da_size(invocation->env));
    sp_da_for(invocation->env, et) {
      spn_dag_hash_u64(&digest, invocation->env[et].key);
      spn_dag_hash_args(&digest, invocation->env[et].values);
    }
    spn_dag_hash_path(&digest, unit->paths.file);
    *identity = spn_dag_hash_final(&digest);

    spn_dag_object_ctx_t* ctx = sp_alloc_type(b->mem, spn_dag_object_ctx_t);
    *ctx = (spn_dag_object_ctx_t) {
      .unit = unit,
      .invocation = invocation,
    };
    spn_dag_action_config_t config = {
      .kind = SPN_DAG_ACTION_DISCOVERED,
      .identity = *identity,
      .execute = spn_dag_exec_object,
      .user_data = ctx,
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
  spn_path_t path = spn_path_join(mem, unit->paths.object, name);
  sp_mem_end_scratch(s);
  return path;
}

spn_err_t spn_dag_build_add_target(spn_dag_build_t* b, spn_target_unit_t* target, const spn_target_plan_t* plan) {
  spn_dag_t* g = b->graph;
  spn_toolchain_unit_t* toolchain = target->pkg->build->toolchain;

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
      .execute = spn_dag_exec_embed,
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

  sp_da(spn_dag_id_t) inputs = sp_da_new(b->mem, spn_dag_id_t);
  sp_da(spn_arg_t) objects = sp_da_new(b->mem, spn_arg_t);
  sp_da_for(target->objects, it) {
    spn_dag_object_ids_t* object = sp_ht_getp(b->ids.objects, target->objects[it]);
    sp_assert(object);
    sp_da_push(inputs, object->object);
    sp_da_push(objects, spn_arg_path(dag_artifact_declared(g, object->object)));
  }
  if (ids.embed.object.occupied) {
    sp_da_push(inputs, ids.embed.object);
    sp_da_push(objects, spn_arg_path(dag_artifact_declared(g, ids.embed.object)));
  }

  if (spn.host.os == SPN_OS_WINDOWS) {
    spn_dag_rsp_ctx_t* rsp = sp_alloc_type(b->mem, spn_dag_rsp_ctx_t);
    rsp->style = spn_rsp_style(toolchain->cc.driver);
    rsp->args = objects;

    spn_path_t path = spn_path_join(b->mem, target->paths.object, sp_str_lit("objects.rsp"));

    spn_digest_ctx_t digest = sp_zero;
    spn_digest_init_blake3(&digest);
    spn_dag_hash_str(&digest, sp_str_lit("spn.build.rsp.v1"));
    spn_dag_hash_u8(&digest, (u8)rsp->style);
    sp_for(it, SPN_PATH_ROOT_COUNT) {
      spn_dag_hash_str(&digest, g->roots->dirs[it]);
    }
    spn_dag_hash_args(&digest, rsp->args);

    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .identity = spn_dag_hash_final(&digest),
      .execute = spn_dag_exec_rsp,
      .user_data = rsp,
    });
    spn_dag_id_t file = spn_dag_add_file(g, path);
    spn_try(spn_dag_action_add_output(g, action, file));

    sp_da_push(inputs, file);
    objects = sp_da_new(b->mem, spn_arg_t);
    sp_da_push(objects, spn_arg_glue(sp_str_lit("@"), path));
  }

  spn_dag_target_ctx_t* ctx = sp_alloc_type(b->mem, spn_dag_target_ctx_t);
  *ctx = (spn_dag_target_ctx_t) {
    .target = target,
    .link = &plan->link.cc,
    .objects = objects,
  };

  switch (target->kind) {
    case SPN_CC_OUTPUT_REACTOR:
    case SPN_CC_OUTPUT_SHARED_LIB: {
      spn_cc_exports_format_t format = spn_cc_exports_format(target->kind, spn_os_to_native_object_format(target->pkg->build->profile.os));
      spn_digest_ctx_t digest = sp_zero;
      spn_digest_init_blake3(&digest);
      spn_dag_hash_str(&digest, sp_str_lit("spn.build.exports.v6"));
      spn_dag_hash_u64(&digest, toolchain->identity);
      spn_dag_hash_u8(&digest, (u8)format);
      spn_dag_hash_u8(&digest, (u8)spn_rsp_style(toolchain->cc.driver));
      spn_dag_hash_str(&digest, target->info->name);
      spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .identity = spn_dag_hash_final(&digest),
        .execute = spn_dag_exec_exports,
        .user_data = ctx,
      });
      ids.exports = spn_dag_add_file(g, plan->link.cc.exports);
      spn_try(spn_dag_action_add_output(g, action, ids.exports));

      sp_da_for(inputs, it) {
        spn_dag_action_add_input(g, action, inputs[it]);
      }
      sp_da_for(plan->link.cc.whole_archives, it) {
        spn_dag_action_add_input(g, action, spn_dag_add_file(g, plan->link.cc.whole_archives[it]));
      }
      break;
    }
    case SPN_CC_OUTPUT_EXE:
    case SPN_CC_OUTPUT_STATIC_LIB: {
      break;
    }
    case SPN_CC_OUTPUT_OBJECT: {
      sp_unreachable_case();
    }
  }

  spn_path_t output = target->paths.output;
  switch (target->kind) {
    case SPN_CC_OUTPUT_STATIC_LIB: {
      spn_digest_ctx_t digest = sp_zero;
      spn_digest_init_blake3(&digest);
      spn_dag_hash_str(&digest, sp_str_lit("spn.build.archive.v1"));
      spn_dag_hash_u64(&digest, target->pkg->fingerprint);
      spn_dag_hash_str(&digest, target->pkg->info->name);
      spn_dag_hash_str(&digest, target->info->name);
      ids.action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .identity = spn_dag_hash_final(&digest),
        .execute = spn_dag_exec_archive,
        .user_data = ctx,
      });
      sp_da_for(inputs, it) {
        spn_dag_action_add_input(g, ids.action, inputs[it]);
      }
      ids.output = spn_dag_add_file(g, output);
      spn_try(spn_dag_action_add_output(g, ids.action, ids.output));
      break;
    }
    case SPN_CC_OUTPUT_EXE:
    case SPN_CC_OUTPUT_SHARED_LIB:
    case SPN_CC_OUTPUT_REACTOR: {
      spn_digest_ctx_t digest = sp_zero;
      spn_digest_init_blake3(&digest);
      spn_dag_hash_str(&digest, sp_str_lit("spn.build.link.v7"));
      spn_dag_hash_u64(&digest, target->pkg->fingerprint);
      spn_dag_hash_str(&digest, ctx->link->pkg);
      spn_dag_hash_str(&digest, ctx->link->name);
      spn_dag_hash_u8(&digest, (u8)ctx->link->lang);
      spn_dag_hash_u8(&digest, (u8)ctx->link->kind);
      spn_dag_hash_strs(&digest, ctx->link->libs);
      spn_dag_hash_strs(&digest, ctx->link->private_libs);
      spn_dag_hash_strs(&digest, ctx->link->system_libs);
      spn_dag_hash_paths(&digest, ctx->link->lib_dirs);
      spn_dag_hash_strs(&digest, ctx->link->frameworks);
      spn_dag_hash_strs(&digest, ctx->link->args);
      spn_dag_hash_paths(&digest, ctx->link->scripts);
      spn_dag_hash_u64(&digest, ctx->link->min_os.major);
      spn_dag_hash_u64(&digest, ctx->link->min_os.minor);
      spn_dag_hash_u8(&digest, (u8)ctx->link->subsystem);
      ids.action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .identity = spn_dag_hash_final(&digest),
        .execute = spn_dag_exec_link,
        .user_data = ctx,
      });
      sp_da_for(inputs, it) {
        spn_dag_action_add_input(g, ids.action, inputs[it]);
      }
      sp_da_for(plan->link.cc.scripts, it) {
        spn_dag_action_add_input(g, ids.action, spn_dag_add_file(g, plan->link.cc.scripts[it]));
      }
      if (ids.exports.occupied) {
        spn_dag_action_add_input(g, ids.action, ids.exports);
      }
      ids.output = spn_dag_add_file(g, output);
      spn_try(spn_dag_action_add_output(g, ids.action, ids.output));
      if (!spn_path_empty(plan->link.cc.implib)) {
        spn_try(spn_dag_action_add_output(g, ids.action, spn_dag_add_file(g, plan->link.cc.implib)));
      }
      break;
    }
    case SPN_CC_OUTPUT_OBJECT: {
      sp_unreachable_case();
    }
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

  source_pin_t pin = source_pin(unit);
  spn_pkg_unit_header_maps_t published = spn_pkg_unit_header_maps(unit);

  spn_digest_ctx_t digest = sp_zero;
  spn_digest_init_blake3(&digest);
  spn_dag_hash_str(&digest, sp_str_lit("spn.build.tree.v11"));
  spn_dag_hash_str(&digest, unit->info->qualified);
  hash_pin(&digest, &pin);
  sp_for(mt, published.count) {
    sp_om_for(published.maps[mt], it) {
      spn_dag_hash_paths(&digest, sp_str_om_at(published.maps[mt], it)->headers);
    }
  }
  sp_da_for(unit->info->publish.copy, it) {
    spn_dag_hash_u8(&digest, (u8)unit->info->publish.copy[it].tree);
    spn_dag_hash_str(&digest, unit->info->publish.copy[it].pattern);
    spn_dag_hash_str(&digest, unit->info->publish.copy[it].dest);
  }

  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .kind = SPN_DAG_ACTION_DISCOVERED,
    .identity = spn_dag_hash_final(&digest),
    .execute = spn_dag_exec_tree,
    .user_data = unit,
  });
  spn_try(spn_dag_action_add_output(g, action, spn_dag_add_tree(g, unit->paths.include)));
  spn_dag_id_t stamp = spn_dag_add_output(g, sp_str_lit("include.stamp"));
  spn_try(spn_dag_action_add_output(g, action, stamp));
  sp_ht_insert(b->ids.stamps, unit->paths.include, stamp);

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

static void dag_add_link_deps(spn_dag_build_t* b, const spn_target_plan_t* plan, spn_dag_id_t action) {
  spn_dag_t* g = b->graph;

  sp_da_for(plan->link.libs, it) {
    spn_dag_target_ids_t* dep = sp_ht_getp(b->ids.targets, plan->link.libs[it].lib);
    if (dep) {
      spn_dag_action_add_input(g, action, dep->output);
    }
  }
}

static void dag_add_target_edges(spn_dag_build_t* b, spn_target_unit_t* target, const spn_target_plan_t* plan) {
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
  sp_da_for(plan->include, it) {
    spn_dag_id_t* stamp = sp_ht_getp(b->ids.stamps, plan->include[it]);
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
    dag_add_link_deps(b, plan, target_ids.action);
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

static spn_err_t add_compile_commands(spn_dag_build_t* b) {
  spn_dag_t* g = b->graph;
  spn_session_t* session = b->session;

  spn_digest_ctx_t digest = sp_zero;
  spn_digest_init_blake3(&digest);
  spn_dag_hash_str(&digest, sp_str_lit("spn.build.compile_commands.v2"));
  sp_for(it, SPN_PATH_ROOT_COUNT) {
    spn_dag_hash_str(&digest, g->roots->dirs[it]);
  }
  spn_dag_hash_u64(&digest, sp_om_size(session->units.objects));
  sp_assert(sp_om_size(session->dag.objects) == sp_om_size(session->units.objects));
  sp_om_for(session->units.objects, it) {
    spn_compile_unit_t* unit = sp_om_at(session->units.objects, it);
    spn_dag_hash_digest(&digest, *sp_om_get(session->dag.objects, unit->id));
    spn_dag_hash_path(&digest, unit->paths.object);
  }

  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = spn_dag_hash_final(&digest),
    .execute = spn_dag_exec_compile_commands,
    .user_data = session,
  });
  b->compile_commands = spn_dag_add_output(g, sp_str_lit("compile_commands.json"));
  spn_dag_id_t stamp = spn_dag_add_output(g, sp_str_lit("compile_commands.stamp"));
  spn_try(spn_dag_action_add_output(g, action, b->compile_commands));
  spn_try(spn_dag_action_add_output(g, action, stamp));

  sp_ht_for_kv(b->ids.objects, it) {
    spn_dag_action_add_input(g, it.val->action, stamp);
  }

  return SPN_OK;
}

static spn_err_t prepare_graph(spn_dag_build_t* b) {
  spn_session_t* session = b->session;

  sp_om_for(session->units.builds, i) {
    spn_build_unit_t* build = sp_om_at(session->units.builds, i);
    sp_da_for(build->packages, j) {
      spn_pkg_unit_t* package = build->packages[j];
      sp_da_for(package->targets, k) {
        spn_target_unit_t* target = package->targets[k];
        spn_try(spn_dag_build_add_target(b, target, spn_session_get_target_plan(session, target->id)));
      }
    }
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

  sp_om_for(session->units.builds, i) {
    spn_build_unit_t* build = sp_om_at(session->units.builds, i);
    sp_da_for(build->packages, j) {
      spn_pkg_unit_t* package = build->packages[j];
      sp_da_for(package->targets, k) {
        spn_target_unit_t* target = package->targets[k];
        dag_add_target_edges(b, target, spn_session_get_target_plan(session, target->id));
      }
    }
  }

  spn_try(add_compile_commands(b));

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

  const spn_path_roots_t* roots = b->graph->roots;
  sp_fs_create_parent_at(spn_path_at(roots, to));
  sp_err_t copied = sp_fs_copy_file_at(spn_path_at(roots, artifact->materialized), spn_path_at(roots, to), SP_FS_ATOMIC_REPLACE);

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

static void dag_stage_sync(spn_dag_build_t* b, spn_path_t manifest, sp_da(spn_stage_record_t) live) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  const spn_path_roots_t* roots = b->graph->roots;

  sp_str_t previous = sp_zero;
  sp_io_read_file_at(scratch.mem, spn_path_at(roots, manifest), &previous);
  spn_stage_plan_t plan = spn_stage_plan(scratch.mem, previous, live);

  sp_da_for(plan.dropped, it) {
    spn_path_t dropped = { .root = manifest.root, .sub = plan.dropped[it] };
    sp_fs_remove_file_at(spn_path_at(roots, dropped));
    spn_dag_file_cache_invalidate(b->env.files, dropped);
  }

  sp_io_dyn_mem_writer_t sink = sp_zero;
  sp_io_dyn_mem_writer_init(scratch.mem, &sink);
  spn_stage_render(&sink.base, plan.records);
  sp_fs_create_parent_at(spn_path_at(roots, manifest));
  sp_fs_write_atomic_at(spn_path_at(roots, manifest), sp_io_dyn_mem_writer_as_str(&sink));
  sp_mem_end_scratch(scratch);
}

static spn_err_t dag_stage(spn_dag_build_t* b) {
  spn_session_t* session = b->session;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = SPN_OK;

  sp_da_for(session->plans.build, i) {
    spn_build_plan_t* plan = &session->plans.build[i];
    spn_path_t root = plan->build->paths.root;

    sp_da(spn_stage_record_t) live = sp_da_new(scratch.mem, spn_stage_record_t);
    sp_da_for(plan->staged, j) {
      spn_stage_closure_t* closure = &plan->staged[j];
      sp_da_push(live, ((spn_stage_record_t) { .owner = closure->exe.path.sub, .path = closure->exe.path.sub }));
      sp_da_for(closure->libs, lt) {
        sp_da_push(live, ((spn_stage_record_t) { .owner = closure->exe.path.sub, .path = closure->libs[lt].path.sub }));
      }
    }
    dag_stage_sync(b, spn_path_join(scratch.mem, root, sp_str_lit(".spn/staged")), live);

    sp_ht(spn_path_t, bool) copied = SP_NULLPTR;
    sp_ht_init(scratch.mem, copied);
    sp_ht_set_fns(copied, spn_path_on_hash, spn_path_on_compare);
    sp_da_for(plan->staged, j) {
      spn_stage_closure_t* closure = &plan->staged[j];
      spn_dag_target_ids_t* ids = sp_ht_getp(b->ids.targets, closure->exe.target);
      if (!ids) {
        continue;
      }
      err = dag_stage_copy(b, ids->output, closure->exe.path);
      if (err) {
        goto done;
      }
      sp_da_for(closure->libs, k) {
        spn_stage_entry_t* lib = &closure->libs[k];
        spn_dag_target_ids_t* lib_ids = sp_ht_getp(b->ids.targets, lib->target);
        if (!lib_ids || sp_ht_getp(copied, lib->path)) {
          continue;
        }
        sp_ht_insert(copied, lib->path, true);
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

  sp_da_for(session->plans.build, it) {
    spn_build_unit_t* build = session->plans.build[it].build;
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
  const spn_path_roots_t* roots = &op->ctx->roots;
  spn_dag_build_t* b = sp_alloc_type(session->mem, spn_dag_build_t);
  sp_mem_zero(b, sizeof(spn_dag_build_t));
  b->session = session;
  b->mem = spn.mem;
  b->graph = spn_dag_new(spn.mem, roots);
  sp_ht_init(b->mem, b->ids.user_outputs);
  sp_ht_init(b->mem, b->ids.stamps);
  sp_ht_set_fns(b->ids.stamps, spn_path_on_hash, spn_path_on_compare);
  sp_ht_init(b->mem, b->ids.targets);
  sp_ht_init(b->mem, b->ids.objects);

  spn_path_t root = spn_path_anchor(session->mem, roots, spn_path_join(session->mem, spn_path_from_root(SPN_PATH_ROOT_CACHE), sp_str_lit("dag")));
  spn_path_t tmp = spn_path_join(session->mem, root, sp_str_lit("tmp"));
  sp_fs_create_dir_at(spn_path_at(roots, tmp));

  spn_dag_store_init(&b->store, (spn_dag_store_config_t) {
    .kind = SPN_DAG_STORE_FILESYSTEM,
    .mem = spn.mem,
    .roots = roots,
    .dir = spn_path_join(session->mem, root, sp_str_lit("store")),
  });
  spn_dag_action_cache_init(&b->actions, spn.mem, roots, spn_path_join(session->mem, root, sp_str_lit("strong")));
  spn_dag_obs_table_init(&b->discovery, spn.mem, roots, spn_path_join(session->mem, root, sp_str_lit("weak")));
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
    spn_path_t to = spn_path_join(scratch.mem, session->paths.root, sp_str_lit("compile_commands.json"));
    sp_da(spn_stage_record_t) live = sp_da_new(scratch.mem, spn_stage_record_t);
    sp_da_push(live, ((spn_stage_record_t) { .owner = to.sub, .path = to.sub }));
    dag_stage_sync(b, spn_path_join(scratch.mem, session->paths.build, sp_str_lit(".spn/staged")), live);
    spn_err_t staged = dag_stage_copy(b, b->compile_commands, to);
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
