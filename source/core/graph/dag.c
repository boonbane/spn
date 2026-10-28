#include "compiler/types.h"
#include "sp.h"
#include "io/io.h"
#include "fs/fs.h"
#include "macro/macro.h"
#include "project/project.h"
#include "ctx/types.h"
#include "error/error.h"
#include "spn/core.h"
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
#include "external/zig.h"
#include "op/op.h"
#include "paths/paths.h"
#include "semver/convert.h"
#include "str/str.h"
#include "session/session.h"
#include "thread_pool/thread_pool.h"
#include "dag/run.h"
#include "unit/unit.h"
#include "graph/build.h"
#include "graph/dag.h"
#include "graph/nodes/nodes.h"
#include "triple/triple.h"

static spn_dag_digest_t hash_embedding(const spn_path_roots_t* roots, spn_target_unit_t* target) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.embed.v9"));
  spn_dag_hash_str(&ctx, target->pkg->eval.manifest->qualified.str);
  spn_dag_hash_str(&ctx, target->eval->info->name.str);
  spn_dag_hash_u8(&ctx, (u8)target->eval->info->kind);
  spn_profile_info_t* profile = &target->pkg->build->profile;
  spn_dag_hash_u8(&ctx, (u8)profile->os);
  spn_dag_hash_u8(&ctx, (u8)profile->arch);
  spn_dag_hash_u8(&ctx, (u8)profile->abi);
  si_da_for(target->eval->embed, it) {
    spn_embed_t* embed = &target->eval->embed[it];
    spn_dag_hash_u8(&ctx, (u8)embed->kind);
    spn_dag_hash_path(&ctx, roots, embed->path);
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

spn_event_subject_t spn_target_unit_subject(const spn_target_unit_t* unit) {
  return (spn_event_subject_t) {
    .pkg = unit->pkg->eval.manifest->name.str,
    .target = unit->eval->info->name.str,
  };
}

//////////////////
// CONSTRUCTION //
//////////////////
static bool pkg_publishes(spn_pkg_unit_t* unit) {
  if (spn_pkg_unit_is_script_host(unit)) {
    return false;
  }
  if (!si_da_empty(unit->eval.publish)) {
    return true;
  }
  si_om_for(unit->eval.targets, it) {
    if (!si_da_empty(si_om_at(unit->eval.targets, it)->publish)) {
      return true;
    }
  }
  return false;
}

static void announce_package(spn_dag_build_t* b, spn_dag_id_t action, spn_pkg_unit_t* pkg) {
  sp_ht_insert(b->announce.pkgs, action, pkg);
  if (!sp_ht_getp(b->announce.announced, pkg)) {
    sp_ht_insert(b->announce.announced, pkg, (sp_atomic_s32_t)0);
  }
}

static void dag_trace(const spn_dag_trace_event_t* event, void* user_data) {
  spn_dag_build_t* b = (spn_dag_build_t*)user_data;
  if (event->kind != SPN_DAG_TRACE_EXECUTE) {
    return;
  }
  spn_pkg_unit_t** pkg = sp_ht_getp(b->announce.pkgs, event->action);
  if (!pkg) {
    return;
  }
  if (!sp_atomic_s32_cas(sp_ht_getp(b->announce.announced, *pkg), 0, 1, SP_ATOMIC_SEQ_CST)) {
    return;
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_COMPILE_START,
    .pkg = (*pkg)->eval.manifest->name.str,
    .compile_start = {
      .version = spn_semver_to_str(spn.events->mem, (*pkg)->eval.manifest->version),
    },
  });
}

spn_err_t spn_dag_build_add_build(spn_dag_build_t* b, spn_build_unit_t* build) {
  spn_dag_t* g = b->graph;
  sp_assert(!sp_ht_getp(b->ids.builds, build));

  spn_dag_id_t libc = sp_zero;
  switch (build->profile.sdk.kind) {
    case SPN_SDK_NONE:
    case SPN_SDK_SYSROOT:
    case SPN_SDK_MACOS:
    case SPN_SDK_MSVC: {
      break;
    }
    case SPN_SDK_LIBC: {
      spn_libc_t* paths = &build->profile.sdk.libc;

      spn_digest_ctx_t digest = sp_zero;
      spn_digest_init_blake3(&digest);
      spn_dag_hash_str(&digest, sp_str_lit("spn.build.libc.v3"));
      spn_dag_hash_path(&digest, g->roots, paths->include);
      spn_dag_hash_path(&digest, g->roots, paths->sys_include);
      spn_dag_hash_path(&digest, g->roots, paths->crt);
      spn_dag_hash_path(&digest, g->roots, paths->msvc_lib);
      spn_dag_hash_path(&digest, g->roots, paths->kernel32_lib);

      si_libc_t* ctx = sp_alloc_type(b->mem, si_libc_t);
      *ctx = (si_libc_t) { .libc = paths };
      spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .identity = spn_dag_hash_final(&digest),
        .execute = si_on_write_libc,
        .user_data = ctx,
      });
      libc = spn_dag_add_output(g, sp_str_lit("libc.txt"));
      ctx->output = libc;
      spn_try(spn_dag_action_add_output(g, action, libc));
      break;
    }
  }

  sp_ht_insert(b->ids.builds, build, libc);
  return SPN_OK;
}

void spn_dag_build_declare_target(spn_dag_build_t* b, spn_target_unit_t* target) {
  spn_dag_t* g = b->graph;
  switch (target->lib_kind) {
    case SPN_LIB_KIND_SOURCE:
    case SPN_LIB_KIND_OBJECT: {
      return;
    }
    case SPN_LIB_KIND_STATIC:
    case SPN_LIB_KIND_SHARED:
    case SPN_LIB_KIND_NONE: {
      break;
    }
  }
  if (sp_da_empty(target->objects)) {
    return;
  }

  spn_dag_target_ids_t ids = {
    .output = spn_dag_add_output(g, target->file_name),
  };
  if (spn_cc_link_implib(&target->pkg->build->profile, target->kind)) {
    spn_triple_t triple = spn_profile_triple(&target->pkg->build->profile);
    ids.implib = spn_dag_add_output(g, spn_triple_lib_file_name(b->mem, triple, target->eval->info->name.str, SP_OS_LIB_STATIC));
  }
  sp_ht_insert(b->ids.targets, target, ids);
}

static void hash_publish(spn_digest_ctx_t* digest, const spn_path_roots_t* roots, const spn_publish_t* publish) {
  spn_dag_hash_u8(digest, (u8)publish->root);
  spn_dag_hash_path(digest, roots, publish->source.path);
  spn_dag_hash_str(digest, publish->dest);
}

typedef struct {
  spn_dag_id_t action;
  spn_dag_id_t object;
} object_ids_t;

static void hash_invocation(spn_digest_ctx_t* digest, const spn_path_roots_t* roots, const spn_invocation_t* base) {
  spn_dag_hash_arg(digest, roots, base->program);
  spn_dag_hash_args(digest, roots, base->args);
  spn_dag_hash_u64(digest, sp_da_size(base->env));
  sp_da_for(base->env, et) {
    spn_dag_hash_u64(digest, base->env[et].key);
    spn_dag_hash_args(digest, roots, base->env[et].values);
  }
}

static spn_dag_digest_t hash_compile(const spn_path_roots_t* roots, const spn_compile_unit_t* unit) {
  spn_digest_ctx_t digest = sp_zero;
  spn_digest_init_blake3(&digest);
  spn_dag_hash_str(&digest, sp_str_lit("spn.build.compile.v7"));
  spn_dag_hash_u64(&digest, unit->target->pkg->build->toolchain->identity);
  hash_invocation(&digest, roots, &unit->plan.base);
  spn_dag_hash_path(&digest, roots, unit->file);
  return spn_dag_hash_final(&digest);
}

static spn_err_t add_object_compilation(spn_dag_build_t* b, sp_mem_t mem, spn_target_unit_t* target, spn_dag_id_t libc, spn_dag_id_t header, si_da(object_ids_t)* objects) {
  spn_dag_t* g = b->graph;
  spn_pkg_unit_t* pkg = target->pkg;

  si_da(spn_dag_id_t) orders = sp_zero;
  sp_da_for(pkg->user_nodes, it) {
    spn_user_node_t* node = &pkg->user_nodes[it];
    si_da_for(node->outputs, ot) {
      si_da_push(mem, orders, spn_dag_add_path(g, node->outputs[ot].path, node->outputs[ot].kind));
    }
  }
  sp_da_for(pkg->deps, it) {
    spn_pkg_unit_t* dep = pkg->deps[it].unit;
    if (!spn_dep_kind_applies(pkg->deps[it].kind, target->eval->info->kind) || !pkg_publishes(dep)) {
      continue;
    }
    si_da_push(mem, orders, spn_dag_add_tree(g, dep->paths.include));
  }
  if (target->eval->info->kind == SPN_TARGET_KIND_EXAMPLE && pkg_publishes(pkg)) {
    si_da_push(mem, orders, spn_dag_add_tree(g, pkg->paths.include));
  }

  sp_da_for(target->objects, it) {
    spn_compile_unit_t* unit = target->objects[it];

    si_compile_t* ctx = sp_alloc_type(b->mem, si_compile_t);
    *ctx = (si_compile_t) {
      .plan = &unit->plan,
      .source = unit->file,
      .subject = spn_target_unit_subject(target),
      .libc = libc,
      .header = header,
    };
    spn_dag_action_config_t config = {
      .identity = hash_compile(g->roots, unit),
      .user_data = ctx,
    };
    switch (unit->plan.depfile) {
      case SPN_CC_DEPFILE_NONE: {
        config.kind = SPN_DAG_ACTION_STATIC;
        config.execute = si_on_compile;
        break;
      }
      case SPN_CC_DEPFILE_OPTIONAL:
      case SPN_CC_DEPFILE_REQUIRED: {
        config.kind = SPN_DAG_ACTION_DISCOVERED;
        config.execute = si_on_compile_discover;
        break;
      }
    }
    object_ids_t ids = sp_zero;
    ids.action = spn_dag_add_action(g, config);
    announce_package(b, ids.action, pkg);
    spn_dag_action_add_input(g, ids.action, spn_dag_add_file(g, unit->file));
    if (libc.occupied) {
      spn_dag_action_add_input(g, ids.action, libc);
    }
    if (header.occupied) {
      spn_dag_action_add_input(g, ids.action, header);
    }
    si_da_for(orders, jt) {
      spn_dag_action_add_order(g, ids.action, orders[jt]);
    }

    ids.object = spn_dag_add_output(g, sp_fs_get_name(unit->name));
    ctx->object = ids.object;
    spn_try(spn_dag_action_add_output(g, ids.action, ids.object));

    si_da_push(mem, *objects, ids);
  }

  return SPN_OK;
}

static spn_err_t add_publish_file(spn_dag_build_t* b, spn_dag_id_t source, spn_path_t dest) {
  spn_dag_t* g = b->graph;

  spn_digest_ctx_t digest = sp_zero;
  spn_digest_init_blake3(&digest);
  spn_dag_hash_str(&digest, sp_str_lit("spn.build.publish.file.v2"));
  spn_dag_hash_path(&digest, g->roots, dest);

  si_publish_file_t* ctx = sp_alloc_type(b->mem, si_publish_file_t);
  *ctx = (si_publish_file_t) {
    .source = source,
    .dest = spn_dag_add_file(g, dest),
  };
  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = spn_dag_hash_final(&digest),
    .execute = si_on_publish_file,
    .user_data = ctx,
  });
  spn_dag_action_add_input(g, action, ctx->source);
  return spn_dag_action_add_output(g, action, ctx->dest);
}

static void hash_link_deps(spn_digest_ctx_t* digest, const spn_link_plan_t* link) {
  si_da_for(link->deps, it) {
    spn_link_dep_t* dep = &link->deps[it];
    spn_dag_hash_u8(digest, (u8)dep->role);
    spn_dag_hash_str(digest, dep->lib->pkg->eval.manifest->qualified.str);
    spn_dag_hash_str(digest, dep->lib->eval->info->name.str);
  }
}

static spn_err_t add_target(spn_dag_build_t* b, sp_mem_t mem, spn_target_unit_t* target) {
  spn_dag_t* g = b->graph;
  spn_build_unit_t* build = target->pkg->build;
  spn_toolchain_unit_t* toolchain = build->toolchain;
  const spn_link_plan_t* link = &target->link;

  spn_dag_id_t* found_libc = sp_ht_getp(b->ids.builds, build);
  sp_assert(found_libc);
  spn_dag_id_t libc = *found_libc;

  si_da(object_ids_t) objects = sp_zero;

  switch (target->lib_kind) {
    case SPN_LIB_KIND_SOURCE: {
      return SPN_OK;
    }
    case SPN_LIB_KIND_OBJECT: {
      spn_try(add_object_compilation(b, mem, target, libc, (spn_dag_id_t) sp_zero, &objects));
      si_da_for(objects, it) {
        spn_try(add_publish_file(b, objects[it].object, spn_path_join(b->mem, target->pkg->paths.lib, target->objects[it]->name)));
      }
      return SPN_OK;
    }
    case SPN_LIB_KIND_STATIC:
    case SPN_LIB_KIND_SHARED:
    case SPN_LIB_KIND_NONE: {
      break;
    }
  }

  if (sp_da_empty(target->objects)) {
    return SPN_OK;
  }

  spn_dag_target_ids_t* ids = sp_ht_getp(b->ids.targets, target);
  sp_assert(ids);

  spn_dag_embed_ids_t embed = sp_zero;
  if (!si_da_empty(target->eval->embed)) {
    sp_str_t name = target->eval->info->name.str;
    si_embed_t* ctx = sp_alloc_type(b->mem, si_embed_t);
    *ctx = (si_embed_t) {
      .target = target,
      .object = spn_dag_add_output(g, sp_fmt(b->mem, "{}.embed.o", sp_fmt_str(name)).value),
      .header = spn_dag_add_output(g, sp_fmt(b->mem, "{}.embed.h", sp_fmt_str(name)).value),
    };
    embed = (spn_dag_embed_ids_t) {
      .action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .kind = SPN_DAG_ACTION_DISCOVERED,
        .identity = hash_embedding(g->roots, target),
        .execute = si_on_embed,
        .user_data = ctx,
      }),
      .object = ctx->object,
      .header = ctx->header,
    };
    announce_package(b, embed.action, target->pkg);
    sp_ht_insert(b->ids.embeds, target, embed);
    spn_try(spn_dag_action_add_output(g, embed.action, embed.object));
    spn_try(spn_dag_action_add_output(g, embed.action, embed.header));
    si_da_for(target->eval->embed, it) {
      spn_embed_t* entry = &target->eval->embed[it];
      if (entry->kind == SPN_EMBED_FILE) {
        spn_dag_action_add_input(g, embed.action, spn_dag_add_file(g, entry->path));
      }
    }
  }

  spn_try(add_object_compilation(b, mem, target, libc, embed.header, &objects));

  si_da(spn_dag_id_t) object_ids = sp_zero;
  si_da_for(objects, it) {
    si_da_push(b->mem, object_ids, objects[it].object);
  }
  if (embed.action.occupied) {
    si_da_push(b->mem, object_ids, embed.object);
  }

  si_da(spn_dag_id_t) link_inputs = sp_zero;
  si_da_for(object_ids, it) {
    si_da_push(mem, link_inputs, object_ids[it]);
  }
  spn_dag_build_objects_t link_objects = { .kind = SPN_DAG_BUILD_OBJECTS_DIRECT, .ids = object_ids };
  if (spn.host.os == SPN_OS_WINDOWS) {
    si_rsp_t* rsp = sp_alloc_type(b->mem, si_rsp_t);
    *rsp = (si_rsp_t) {
      .style = spn_rsp_style(toolchain->cc.driver),
      .objects = object_ids,
    };

    spn_digest_ctx_t digest = sp_zero;
    spn_digest_init_blake3(&digest);
    spn_dag_hash_str(&digest, sp_str_lit("spn.build.rsp.v3"));
    spn_dag_hash_u8(&digest, (u8)rsp->style);
    spn_dag_hash_str(&digest, target->pkg->eval.manifest->qualified.str);
    spn_dag_hash_str(&digest, target->eval->info->name.str);

    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .identity = spn_dag_hash_final(&digest),
      .execute = si_on_write_rsp,
      .user_data = rsp,
    });
    si_da_for(object_ids, it) {
      spn_dag_action_add_input(g, action, object_ids[it]);
    }
    spn_dag_id_t file = spn_dag_add_output(g, sp_str_lit("objects.rsp"));
    rsp->output = file;
    spn_try(spn_dag_action_add_output(g, action, file));

    si_da_push(mem, link_inputs, file);
    link_objects = (spn_dag_build_objects_t) { .kind = SPN_DAG_BUILD_OBJECTS_RSP, .rsp = file };
  }

  switch (target->kind) {
    case SPN_CC_OUTPUT_STATIC_LIB: {
      si_archive_t* ctx = sp_alloc_type(b->mem, si_archive_t);
      *ctx = (si_archive_t) {
        .plan = &target->archive,
        .file_name = target->file_name,
        .subject = spn_target_unit_subject(target),
        .objects = link_objects,
        .output = ids->output,
      };

      spn_digest_ctx_t digest = sp_zero;
      spn_digest_init_blake3(&digest);
      spn_dag_hash_str(&digest, sp_str_lit("spn.build.archive.v3"));
      spn_dag_hash_u64(&digest, toolchain->identity);
      hash_invocation(&digest, g->roots, &target->archive.base);
      spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .identity = spn_dag_hash_final(&digest),
        .execute = si_on_archive,
        .user_data = ctx,
      });
      announce_package(b, action, target->pkg);
      si_da_for(link_inputs, it) {
        spn_dag_action_add_input(g, action, link_inputs[it]);
      }
      spn_try(spn_dag_action_add_output(g, action, ids->output));
      break;
    }
    case SPN_CC_OUTPUT_EXE:
    case SPN_CC_OUTPUT_SHARED_LIB:
    case SPN_CC_OUTPUT_REACTOR: {
      si_link_t* ctx = sp_alloc_type(b->mem, si_link_t);
      *ctx = (si_link_t) {
        .plan = &link->plan,
        .file_name = target->file_name,
        .subject = spn_target_unit_subject(target),
        .objects = link_objects,
        .libc = libc,
        .output = ids->output,
        .implib = ids->implib,
      };
      si_da_for(link->deps, it) {
        spn_link_dep_t* dep = &link->deps[it];
        spn_dag_target_ids_t* lib = sp_ht_getp(b->ids.targets, dep->lib);
        sp_assert(lib);
        switch (dep->role) {
          case SPN_LINK_ROLE_ARCHIVE: {
            si_da_push(b->mem, ctx->deps.archives, lib->output);
            break;
          }
          case SPN_LINK_ROLE_SHARED: {
            si_da_push(b->mem, ctx->deps.shared, lib->implib.occupied ? lib->implib : lib->output);
            break;
          }
          case SPN_LINK_ROLE_WHOLE_ARCHIVE: {
            si_da_push(b->mem, ctx->deps.whole_archives, lib->output);
            break;
          }
        }
      }

      switch (target->kind) {
        case SPN_CC_OUTPUT_SHARED_LIB:
        case SPN_CC_OUTPUT_REACTOR: {
          spn_cc_exports_format_t format = spn_cc_exports_format(target->kind, spn_os_to_native_object_format(build->profile.os));
          si_exports_t* exports = sp_alloc_type(b->mem, si_exports_t);
          *exports = (si_exports_t) {
            .archive = &target->archive,
            .format = format,
            .rsp = spn_rsp_style(toolchain->cc.driver),
            .library = target->eval->info->name.str,
            .subject = spn_target_unit_subject(target),
            .objects = link_objects,
            .whole_archives = ctx->deps.whole_archives,
          };

          spn_digest_ctx_t digest = sp_zero;
          spn_digest_init_blake3(&digest);
          spn_dag_hash_cstr(&digest, "spn.build.exports.v7");
          spn_dag_hash_u64(&digest, toolchain->identity);
          spn_dag_hash_s32(&digest, format);
          spn_dag_hash_s32(&digest, spn_rsp_style(toolchain->cc.driver));
          spn_dag_hash_str(&digest, target->eval->info->name.str);
          hash_link_deps(&digest, link);
          spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
            .identity = spn_dag_hash_final(&digest),
            .execute = si_on_write_exports,
            .user_data = exports,
          });
          announce_package(b, action, target->pkg);
          si_da_for(link_inputs, it) {
            spn_dag_action_add_input(g, action, link_inputs[it]);
          }
          si_da_for(ctx->deps.whole_archives, it) {
            spn_dag_action_add_input(g, action, ctx->deps.whole_archives[it]);
          }
          ctx->exports = spn_dag_add_output(g, sp_fmt(mem, "{}.{}", sp_fmt_str(target->eval->info->name.str), sp_fmt_str(spn_cc_exports_extension(format))).value);
          exports->output = ctx->exports;
          spn_try(spn_dag_action_add_output(g, action, ctx->exports));
          si_da_push(mem, link_inputs, ctx->exports);
          break;
        }
        case SPN_CC_OUTPUT_EXE: {
          break;
        }
        case SPN_CC_OUTPUT_OBJECT:
        case SPN_CC_OUTPUT_STATIC_LIB: {
          sp_unreachable_case();
        }
      }

      spn_digest_ctx_t digest = sp_zero;
      spn_digest_init_blake3(&digest);
      spn_dag_hash_str(&digest, sp_str_lit("spn.build.link.v11"));
      spn_dag_hash_u64(&digest, toolchain->identity);
      hash_invocation(&digest, g->roots, &link->plan.base);
      spn_dag_hash_args(&digest, g->roots, link->plan.tail);
      hash_link_deps(&digest, link);
      spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .identity = spn_dag_hash_final(&digest),
        .execute = si_on_link,
        .user_data = ctx,
      });
      announce_package(b, action, target->pkg);
      si_da_for(link_inputs, it) {
        spn_dag_action_add_input(g, action, link_inputs[it]);
      }
      si_da_for(link->cc.scripts, it) {
        spn_dag_id_t script = spn_dag_add_file(g, link->cc.scripts[it]);
        spn_dag_action_add_input(g, action, script);
        si_da_push(b->mem, ctx->scripts, script);
      }
      si_da_for(ctx->deps.archives, it) {
        spn_dag_action_add_input(g, action, ctx->deps.archives[it]);
      }
      si_da_for(ctx->deps.shared, it) {
        spn_dag_action_add_input(g, action, ctx->deps.shared[it]);
      }
      si_da_for(ctx->deps.whole_archives, it) {
        spn_dag_action_add_input(g, action, ctx->deps.whole_archives[it]);
      }
      if (libc.occupied) {
        spn_dag_action_add_input(g, action, libc);
      }
      spn_try(spn_dag_action_add_output(g, action, ids->output));
      if (ids->implib.occupied) {
        spn_try(spn_dag_action_add_output(g, action, ids->implib));
      }

      if (toolchain->cc.driver == SPN_CC_DRIVER_ZIG) {
        sp_assert(!spn_path_pinned(g->roots, toolchain->cc.cache));

        spn_cc_link_t stub_link = {
          .kind = target->kind,
          .lang = link->cc.lang,
          .system_libs = link->cc.system_libs,
        };
        spn_zig_stub_t stub = spn_zig_stub(b->mem, &build->profile, &stub_link);

        spn_digest_ctx_t warm_digest = sp_zero;
        spn_digest_init_blake3(&warm_digest);
        spn_dag_hash_str(&warm_digest, sp_str_lit("spn.build.warm.v1"));
        spn_dag_hash_u64(&warm_digest, toolchain->identity);
        spn_dag_hash_u64(&warm_digest, toolchain->generation);
        spn_dag_hash_u8(&warm_digest, (u8)stub.triple.arch);
        spn_dag_hash_u8(&warm_digest, (u8)stub.triple.os);
        spn_dag_hash_u8(&warm_digest, (u8)stub.triple.abi);
        spn_dag_hash_u8(&warm_digest, (u8)stub.kind);
        spn_dag_hash_u8(&warm_digest, (u8)stub.lang);
        spn_dag_hash_u8(&warm_digest, (u8)stub.is_static);
        spn_dag_hash_u64(&warm_digest, stub.sanitizers);
        spn_dag_hash_u64(&warm_digest, stub.sdk);
        spn_dag_hash_strs(&warm_digest, stub.system_libs);
        spn_dag_digest_t identity = spn_dag_hash_final(&warm_digest);

        spn_dag_id_t stamp = sp_zero;
        spn_dag_id_t* existing = sp_ht_getp(b->ids.warm, identity);
        if (existing) {
          stamp = *existing;
        }
        else {
          sp_str_t name = spn_zig_stub_name(b->mem, &stub);
          si_zig_warmup_t* warm = sp_alloc_type(b->mem, si_zig_warmup_t);
          *warm = (si_zig_warmup_t) {
            .cc = &toolchain->cc,
            .profile = &build->profile,
            .libc = libc,
            .link = stub_link,
            .name = name,
            .triple = spn_triple_to_str(b->mem, stub.triple),
          };

          spn_dag_id_t warm_action = spn_dag_add_action(g, (spn_dag_action_config_t) {
            .identity = identity,
            .execute = si_on_zig_warmup,
            .user_data = warm,
          });
          spn_dag_action_add_input(g, warm_action, spn_dag_add_file(g, spn_path(b->mem, SPN_DIR_ID_RUNTIME, "zig/stub.c")));
          if (libc.occupied) {
            spn_dag_action_add_input(g, warm_action, libc);
          }
          stamp = spn_dag_add_output(g, name);
          warm->stamp = stamp;
          spn_try(spn_dag_action_add_output(g, warm_action, stamp));
          sp_ht_insert(b->ids.warm, identity, stamp);
        }
        spn_dag_action_add_input(g, action, stamp);
      }
      break;
    }
    case SPN_CC_OUTPUT_OBJECT: {
      sp_unreachable_case();
    }
  }

  spn_pkg_unit_t* pkg = target->pkg;
  switch (target->kind) {
    case SPN_CC_OUTPUT_EXE: {
      switch (target->eval->info->kind) {
        case SPN_TARGET_KIND_EXE:
        case SPN_TARGET_KIND_SCRIPT: {
          spn_try(add_publish_file(b, ids->output, spn_path_join(b->mem, pkg->paths.bin, target->file_name)));
          break;
        }
        case SPN_TARGET_KIND_TEST:
        case SPN_TARGET_KIND_EXAMPLE: {
          break;
        }
        case SPN_TARGET_KIND_LIB:
        case SPN_TARGET_KIND_CONFIGURE_METAPROGRAM:
        case SPN_TARGET_KIND_BUILD_METAPROGRAM: {
          sp_unreachable_case();
        }
      }
      break;
    }
    case SPN_CC_OUTPUT_STATIC_LIB:
    case SPN_CC_OUTPUT_SHARED_LIB: {
      spn_try(add_publish_file(b, ids->output, spn_path_join(b->mem, pkg->paths.lib, target->file_name)));
      if (ids->implib.occupied) {
        spn_try(add_publish_file(b, ids->implib, spn_path_join(b->mem, pkg->paths.lib, spn_dag_find_artifact(g, ids->implib)->name)));
      }
      break;
    }
    case SPN_CC_OUTPUT_REACTOR: {
      break;
    }
    case SPN_CC_OUTPUT_OBJECT: {
      sp_unreachable_case();
    }
  }

  return SPN_OK;
}

spn_err_t spn_dag_build_add_target(spn_dag_build_t* b, spn_target_unit_t* target) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = add_target(b, scratch.mem, target);
  sp_mem_end_scratch(scratch);
  return err;
}

static spn_err_t prepare_graph(spn_dag_build_t* b) {
  spn_session_t* session = b->session;
  spn_dag_t* g = b->graph;

  sp_om_for(session->units.builds, i) {
    spn_build_unit_t* build = sp_om_at(session->units.builds, i);
    spn_try(spn_dag_build_add_build(b, build));
    sp_da_for(build->packages, j) {
      spn_pkg_unit_t* package = build->packages[j];
      sp_da_for(package->targets, k) {
        spn_dag_build_declare_target(b, package->targets[k]);
      }
    }
  }

  sp_om_for(session->units.builds, i) {
    spn_build_unit_t* build = sp_om_at(session->units.builds, i);
    sp_da_for(build->packages, j) {
      spn_pkg_unit_t* package = build->packages[j];
      source_pin_t pin = source_pin(package);

      if (!spn_pkg_unit_is_script_host(package)) {
        spn_dag_id_t script = sp_zero;
        if (package->metaprogram && package->metaprogram->scripts.build) {
          spn_dag_target_ids_t* ids = sp_ht_getp(b->ids.targets, package->metaprogram->scripts.build);
          sp_assert(ids);
          script = ids->output;
        }

        sp_da_for(package->user_nodes, k) {
          spn_user_node_t* node = &package->user_nodes[k];
          sp_assert(script.occupied || sp_str_empty(node->fn));

          spn_digest_ctx_t digest = sp_zero;
          spn_digest_init_blake3(&digest);
          spn_dag_hash_str(&digest, sp_str_lit("spn.build.user.v9"));
          spn_dag_hash_str(&digest, node->unit->eval.manifest->qualified.str);
          spn_dag_hash_u64(&digest, node->unit->fingerprint);
          hash_pin(&digest, &pin);
          spn_dag_hash_str(&digest, node->tag);
          spn_dag_hash_str(&digest, node->fn);
          spn_dag_hash_paths(&digest, g->roots, node->inputs);
          spn_dag_hash_u64(&digest, si_da_size(node->outputs));
          si_da_for(node->outputs, ot) {
            spn_dag_hash_u64(&digest, node->outputs[ot].dir);
            spn_dag_hash_str(&digest, node->outputs[ot].sub);
            spn_dag_hash_u64(&digest, node->outputs[ot].kind);
          }

          si_user_node_t* ctx = sp_alloc_type(b->mem, si_user_node_t);
          *ctx = (si_user_node_t) {
            .node = node,
            .script = script,
          };
          spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
            .kind = SPN_DAG_ACTION_DISCOVERED,
            .identity = spn_dag_hash_final(&digest),
            .execute = si_on_user_node,
            .user_data = ctx,
          });
          announce_package(b, action, package);

          if (script.occupied) {
            spn_dag_action_add_input(g, action, script);
          }
          si_da_for(node->inputs, jt) {
            spn_dag_action_add_input(g, action, spn_dag_add_file(g, node->inputs[jt]));
          }
          si_da_for(node->deps, jt) {
            spn_user_node_t* dep = spn_node_deref(node->deps[jt]);
            si_da_for(dep->outputs, ot) {
              spn_dag_action_add_input(g, action, spn_dag_add_path(g, dep->outputs[ot].path, dep->outputs[ot].kind));
            }
          }

          si_da_for(node->outputs, ot) {
            spn_user_output_t* out = &node->outputs[ot];
            spn_dag_id_t output = spn_dag_add_path(g, out->path, out->kind);
            spn_err_t err = spn_dag_action_add_output(g, action, output);
            if (err) {
              return spn_err_emit(b->session->ctx, (spn_err_union_t) {
                .kind = err,
                .dag = { .path = spn_path_str(g->roots, b->session->ctx->events->mem, out->path) },
              });
            }
          }
        }
      }

      if (pkg_publishes(package)) {
        si_pkg_t* eval = &package->eval;

        spn_digest_ctx_t digest = sp_zero;
        spn_digest_init_blake3(&digest);
        spn_dag_hash_str(&digest, sp_str_lit("spn.build.publish.v2"));
        spn_dag_hash_str(&digest, eval->manifest->qualified.str);
        hash_pin(&digest, &pin);
        si_da_for(eval->publish, it) {
          hash_publish(&digest, g->roots, &eval->publish[it]);
        }
        si_om_for(eval->targets, it) {
          si_target_t* target = si_om_at(eval->targets, it);
          si_da_for(target->publish, jt) {
            hash_publish(&digest, g->roots, &target->publish[jt]);
          }
        }

        si_publish_t* ctx = sp_alloc_type(b->mem, si_publish_t);
        *ctx = (si_publish_t) {
          .pkg = package,
          .include = spn_dag_add_tree(g, package->paths.include),
          .share = spn_dag_add_tree(g, package->paths.share),
        };
        spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
          .kind = SPN_DAG_ACTION_DISCOVERED,
          .identity = spn_dag_hash_final(&digest),
          .execute = si_on_publish,
          .user_data = ctx,
        });
        spn_try(spn_dag_action_add_output(g, action, ctx->include));
        spn_try(spn_dag_action_add_output(g, action, ctx->share));
      }

      sp_da_for(package->targets, k) {
        spn_try(spn_dag_build_add_target(b, package->targets[k]));
      }
    }
  }

  sp_ht_for_kv(b->ids.embeds, it) {
    spn_target_unit_t* target = *it.key;
    si_da_for(target->eval->embed, et) {
      spn_embed_t* embed = &target->eval->embed[et];
      if (embed->kind != SPN_EMBED_DIR) {
        continue;
      }
      sp_da_for(g->artifacts, at) {
        const spn_dag_artifact_t* artifact = &g->artifacts[at];
        if (spn_dag_output_overlaps(artifact, embed->path)) {
          spn_dag_action_add_input(g, it.val->action, artifact->id);
        }
      }
    }
  }

  return SPN_OK;
}

/////////
// RUN //
/////////
static spn_err_t dag_stage_copy(spn_dag_build_t* b, spn_dag_id_t id, spn_path_t to) {
  const spn_dag_artifact_state_t* artifact = spn_dag_run_artifact(&b->run, id);

  sp_sys_file_meta_t staged_meta = sp_zero;
  spn_dag_digest_t staged_digest = sp_zero;
  if (!spn_dag_file_cache_stat(b->run.files, to, &staged_meta) && staged_meta.nlink == 1 &&
      !spn_dag_file_cache_digest(b->run.files, to, &staged_digest) &&
      spn_dag_digest_equal(staged_digest, artifact->digest)) {
    return SPN_OK;
  }

  const spn_path_roots_t* roots = b->graph->roots;
  sp_fs_create_parent_at(spn_path_at(roots, to));
  sp_err_t copied = sp_fs_copy_file_at(spn_path_at(roots, artifact->location), spn_path_at(roots, to), SP_FS_ATOMIC_REPLACE);

  spn_err_t err = copied ? SPN_ERR_DAG_OUTPUT_WRITE : spn_dag_file_cache_seed(b->run.files, to, artifact->digest);
  if (err) {
    spn_dag_file_cache_invalidate(b->run.files, to);
    return spn_err_emit(b->session->ctx, (spn_err_union_t) {
      .kind = err,
      .dag = { .path = spn_path_str(b->graph->roots, b->session->ctx->events->mem, to) },
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

  const spn_path_roots_t* roots = b->graph->roots;
  sp_da_for(session->plans.build, i) {
    spn_build_plan_t* plan = &session->plans.build[i];
    spn_path_t root = plan->build->paths.root;
    spn_path_t manifest = spn_path_join(scratch.mem, root, sp_str_lit(".spn/staged"));

    sp_ht(spn_path_t, bool) exes = SP_NULLPTR;
    sp_ht_init(scratch.mem, exes);
    sp_ht_set_fns(exes, spn_path_on_hash, spn_path_on_compare);
    sp_da(dag_staged_t) next = sp_da_new(scratch.mem, dag_staged_t);
    sp_da_for(plan->staged, j) {
      spn_stage_closure_t* closure = &plan->staged[j];
      sp_ht_insert(exes, closure->exe.path, true);
      sp_da_push(next, ((dag_staged_t) { .exe = closure->exe.path.sub, .entry = closure->exe.path.sub }));
      sp_da_for(closure->libs, lt) {
        sp_da_push(next, ((dag_staged_t) { .exe = closure->exe.path.sub, .entry = closure->libs[lt].path.sub }));
      }
    }

    sp_str_t content = sp_zero;
    sp_io_read_file_at(scratch.mem, spn_path_at(roots, manifest), &content);
    sp_da(sp_str_t) lines = sp_str_split_c8(scratch.mem, content, '\n');
    sp_da(dag_staged_t) previous = sp_da_new(scratch.mem, dag_staged_t);
    sp_da_for(lines, j) {
      s32 tab = sp_str_find_c8(lines[j], '\t');
      if (tab == SP_STR_NO_MATCH) {
        continue;
      }
      dag_staged_t staged = { .exe = sp_str_prefix(lines[j], tab), .entry = sp_str_suffix(lines[j], lines[j].len - tab - 1) };
      sp_da_push(previous, staged);
      spn_path_t exe = { .root = root.root, .sub = staged.exe };
      if (!sp_ht_getp(exes, exe)) {
        sp_da_push(next, staged);
      }
    }

    sp_str_ht(bool) live = SP_NULLPTR;
    sp_str_ht_init(scratch.mem, live);
    sp_io_dyn_mem_writer_t sink = sp_zero;
    sp_io_dyn_mem_writer_init(scratch.mem, &sink);
    sp_da_for(next, j) {
      sp_str_ht_insert(live, next[j].entry, true);
      sp_fmt_io(&sink.base, "{}\t{}\n", sp_fmt_str(next[j].exe), sp_fmt_str(next[j].entry));
    }
    sp_da_for(previous, j) {
      if (sp_str_ht_get(live, previous[j].entry)) {
        continue;
      }
      spn_path_t path = { .root = root.root, .sub = previous[j].entry };
      sp_fs_remove_file_at(spn_path_at(roots, path));
      spn_dag_file_cache_invalidate(b->run.files, path);
    }
    sp_fs_create_parent_at(spn_path_at(roots, manifest));
    sp_fs_write_atomic_at(spn_path_at(roots, manifest), sp_io_dyn_mem_writer_as_str(&sink));

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
  spn_dag_diag_t* diag = &b->run.diag;

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

  const spn_path_roots_t* roots = b->graph->roots;
  sp_mem_t mem = b->session->ctx->events->mem;
  spn_dag_id_t artifact = diag->artifact;
  if (!artifact.occupied && diag->action.occupied) {
    const spn_dag_action_t* action = spn_dag_find_action(b->graph, diag->action);
    if (!sp_da_empty(action->produces)) {
      artifact = action->produces[0];
    }
  }

  sp_str_t path = sp_zero;
  if (!spn_path_empty(diag->path)) {
    path = spn_path_str(roots, mem, diag->path);
  }
  else if (artifact.occupied) {
    const spn_dag_artifact_t* at = spn_dag_find_artifact(b->graph, artifact);
    path = spn_path_empty(at->path) ? at->name : spn_path_str(roots, mem, at->path);
  }

  return spn_err_emit(b->session->ctx, (spn_err_union_t) {
    .kind = diag->err,
    .dag = { .path = path },
  });
}

static void dag_emit_reports(spn_dag_build_t* b, u64 elapsed) {
  spn_session_t* session = b->session;
  bool failed = b->result != SPN_OK;
  u32 hits = (u32)sp_atomic_s32_load(&b->run.progress.hits, SP_ATOMIC_SEQ_CST);
  u32 misses = (u32)sp_atomic_s32_load(&b->run.progress.misses, SP_ATOMIC_SEQ_CST);

  sp_da_for(session->plans.build, it) {
    spn_build_plan_t* plan = &session->plans.build[it];
    spn_build_unit_t* build = plan->build;
    const si_pkg_info_t* pkg = plan->root->eval.manifest;
    spn_profile_info_t* profile = &build->profile;

    if (failed) {
      spn_event_buffer_push(session->ctx->events, (spn_event_t) {
        .kind = SPN_EVENT_BUILD_FAILED,
        .pkg = pkg->name.str,
        .build_failed = {
          .profile = profile->name.str,
          .time = elapsed,
        },
      });
    }
    else {
      spn_event_buffer_push(session->ctx->events, (spn_event_t) {
        .kind = SPN_EVENT_BUILD_PASSED,
        .pkg = pkg->name.str,
        .build_passed = {
          .profile = profile->name.str,
          .time = elapsed,
          .hits = hits,
          .misses = misses,
        },
      });
    }

    spn_event_buffer_push(session->ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_BUILD_SUMMARY,
      .pkg = pkg->name.str,
      .build_summary = {
        .success = !failed,
        .hits = hits,
        .misses = misses,
        .total = (u32)sp_da_size(b->graph->actions),
        .time = elapsed,
        .profile = profile->name.str,
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
  sp_ht_init(b->mem, b->ids.targets);
  sp_ht_init(b->mem, b->ids.embeds);
  sp_ht_init(b->mem, b->ids.builds);
  sp_ht_init(b->mem, b->ids.warm);
  sp_ht_init(b->mem, b->announce.pkgs);
  sp_ht_init(b->mem, b->announce.announced);

  spn_path_t root = spn_path_anchor(session->mem, roots, spn_path_from_id(SPN_DIR_ID_DAG));
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

  b->run = (spn_dag_run_t) {
    .g = b->graph,
    .mem = b->mem,
    .files = &session->dag.files,
    .cache = &b->actions,
    .store = &b->store,
    .discovery = &b->discovery,
    .scratch = tmp,
    .trace = dag_trace,
    .trace_data = b,
    .cancel = &op->cancelled,
    .wake = &op->ctx->wake,
  };

  return b;
}

spn_err_t spn_dag_build_run(spn_dag_build_t* b, u32 workers) {
  spn_thread_pool_init(&b->pool, spn.mem, (spn_thread_pool_config_t) {
    .workers = sp_min(workers, (u32)sp_da_size(b->graph->actions)),
    .on_worker_exit = spn_wasm_thread_exit,
  });

  b->timer = sp_tm_start_timer();
  spn_dag_file_cache_invalidate_all(b->run.files);
  b->result = spn_dag_run(&b->run, &b->pool);
  spn_thread_pool_deinit(&b->pool);
  spn_dag_file_cache_flush(b->run.files, b->session->dag.files_path);
  return dag_result(b);
}

spn_err_t spn_dag_build_session(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_project_t* project = session->project;

  spn_dag_build_t* dag = spn_dag_build_new(op);
  session->dag.build = dag;

  spn_try(prepare_graph(dag));

  spn_build_unit_t* unit = session->units.target;
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_INIT_BUILD_GRAPH,
    .pkg = session->manifest->name.str,
    .graph_init = {
      .profile = unit->profile.name.str,
      .target = spn_triple_to_str(spn.events->mem, spn_profile_triple(&unit->profile)),
      .toolchain = unit->toolchain->info->name.str,
      .version = unit->toolchain->version,
      .force = session->force,
    }
  });

  sp_atomic_ptr_store(&session->ctx->progress, &dag->run.progress, SP_ATOMIC_SEQ_CST);
  spn_err_t result = spn_dag_build_run(dag, spn_cpu_count());
  sp_atomic_ptr_store(&session->ctx->progress, SP_NULLPTR, SP_ATOMIC_SEQ_CST);
  u64 elapsed = sp_tm_read_timer(&dag->timer);

  if (dag->result == SPN_ERR_DAG_CANCELLED) {
    return result;
  }

  spn_err_t written = spn_dag_build_write_compile_commands(dag);
  if (!result) {
    result = written;
  }
  if (!result) {
    if (!sp_fs_exists_at(spn_path_at(&session->ctx->roots, project->paths.lock))) {
      spn_try(spn_project_update_lock(session->ctx, project, session->resolve));
    }
    result = dag_stage(dag);
    spn_dag_file_cache_flush(dag->run.files, session->dag.files_path);
  }
  if (!dag->result) {
    dag->result = result;
  }

  dag_emit_reports(dag, elapsed);

  return result;
}
