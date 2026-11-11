#include "compiler/types.h"
#include "hash_table/hash_table.h"
#include "sp.h"
#include "io/io.h"
#include "fs/fs.h"
#include "macro/macro.h"
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

static spn_dag_digest_t hash_embedding(const spn_path_roots_t* roots, spn_target_unit_t* unit) {
  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.build.embed.v9"));
  spn_dag_hash_str(&ctx, unit->pkg->pkg.manifest->name.str);
  spn_dag_hash_str(&ctx, unit->target->info->name.str);
  spn_dag_hash_u8(&ctx, (u8)unit->target->info->kind);
  spn_profile_info_t* profile = &unit->pkg->build->profile;
  spn_dag_hash_u8(&ctx, (u8)profile->target.os);
  spn_dag_hash_u8(&ctx, (u8)profile->target.arch);
  spn_dag_hash_u8(&ctx, (u8)profile->target.abi);
  si_da_for(unit->target->embed, it) {
    spn_embed_t* embed = &unit->target->embed[it];
    spn_dag_hash_u8(&ctx, (u8)embed->kind);
    spn_dag_hash_path(&ctx, roots, embed->path);
    spn_dag_hash_str(&ctx, embed->dest);
    spn_dag_hash_str(&ctx, embed->types.data);
    spn_dag_hash_str(&ctx, embed->types.size);
  }
  return spn_dag_hash_final(&ctx);
}

static void hash_pin(spn_digest_ctx_t* ctx, const spn_source_pin_t* pin) {
  spn_dag_hash_u8(ctx, (u8)pin->kind);
  spn_dag_hash_str(ctx, pin->rev);
  spn_dag_hash_str(ctx, pin->dir);
  spn_dag_hash_u64(ctx, pin->patches);
}

spn_event_subject_t spn_target_unit_subject(const spn_target_unit_t* unit) {
  return (spn_event_subject_t) {
    .pkg = unit->pkg->pkg.manifest->name.str,
    .target = unit->target->info->name.str,
  };
}

//////////////////
// CONSTRUCTION //
//////////////////
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
  spn_dag_id_t action = event->action;
  u64 index = 0;
  spn_pkg_unit_t** pkg = sp_ht_get_ex(b->announce.pkgs, action, index);
  if (!pkg) {
    return;
  }
  if (!sp_atomic_s32_cas(sp_ht_get_ex(b->announce.announced, *pkg, index), 0, 1, SP_ATOMIC_SEQ_CST)) {
    return;
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_COMPILE_START,
    .pkg = (*pkg)->pkg.manifest->name.str,
    .compile_start = {
      .version = spn_semver_to_str(spn.events->mem, (*pkg)->pkg.manifest->version),
    },
  });
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
  spn_digest_init(&digest);
  spn_dag_hash_str(&digest, sp_str_lit("spn.build.compile.v7"));
  spn_dag_hash_u64(&digest, unit->target->pkg->build->toolchain->identity);
  hash_invocation(&digest, roots, &unit->plan.base);
  spn_dag_hash_path(&digest, roots, unit->file);
  return spn_dag_hash_final(&digest);
}

static spn_err_t add_object_compilation(spn_dag_build_t* b, sp_mem_t mem, spn_target_unit_t* target, spn_dag_id_t libc, spn_dag_id_t header, si_da(spn_dag_id_t) orders, si_da(object_ids_t)* objects) {
  spn_dag_t* g = b->graph;
  spn_pkg_unit_t* pkg = target->pkg;

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
  spn_digest_init(&digest);
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
    spn_dag_hash_str(digest, dep->lib->pkg->pkg.manifest->name.str);
    spn_dag_hash_str(digest, dep->lib->target->info->name.str);
  }
}

static spn_err_t add_target(spn_dag_build_t* b, sp_mem_t mem, spn_target_unit_t* unit) {
  spn_dag_t* g = b->graph;
  spn_build_unit_t* build = unit->pkg->build;
  spn_toolchain_unit_t* toolchain = build->toolchain;
  const spn_link_plan_t* link = &unit->link;
  const spn_target_emit_t* emit = &unit->emit;

  spn_dag_id_t* found_libc = sp_ht_getp(b->ids.builds, build);
  sp_assert(found_libc);
  spn_dag_id_t libc = *found_libc;

  si_da(spn_dag_id_t) needs = sp_zero;
  si_dag_pkg_ids_t* pkg_ids = si_om_get(b->ids.packages, unit->pkg);
  sp_assert(pkg_ids);

  si_da_for(pkg_ids->outputs, it) {
    si_da_push(mem, needs, pkg_ids->outputs[it]);
  }
  si_da_for_ptr(unit->edges, edge) {
    si_dag_pkg_ids_t* dep = si_om_get(b->ids.packages, edge->pkg);
    sp_assert(dep);
    if (dep->include.occupied) {
      si_da_push(mem, needs, dep->include);
    }
  }

  if (sp_da_empty(unit->objects)) {
    return SPN_OK;
  }

  si_da(object_ids_t) objects = sp_zero;
  if (emit->objects) {
    spn_try(add_object_compilation(b, mem, unit, libc, (spn_dag_id_t) sp_zero, needs, &objects));
    si_da_for(objects, it) {
      spn_try(add_publish_file(b, objects[it].object, spn_path_join(b->mem, unit->install, unit->objects[it]->name)));
    }
    return SPN_OK;
  }

  spn_dag_target_ids_t* ids = sp_ht_getp(b->ids.targets, unit);
  sp_assert(ids);

  spn_dag_embed_ids_t embed = sp_zero;
  if (!si_da_empty(unit->target->embed)) {
    sp_str_t name = unit->target->info->name.str;
    si_embed_t* ctx = sp_alloc_type(b->mem, si_embed_t);
    *ctx = (si_embed_t) {
      .unit = unit,
      .object = spn_dag_add_output(g, sp_fmt(b->mem, "{}.embed.o", sp_fmt_str(name)).value),
      .header = spn_dag_add_output(g, sp_fmt(b->mem, "{}.embed.h", sp_fmt_str(name)).value),
    };
    embed = (spn_dag_embed_ids_t) {
      .action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .kind = SPN_DAG_ACTION_DISCOVERED,
        .identity = hash_embedding(g->roots, unit),
        .execute = si_on_embed,
        .user_data = ctx,
      }),
      .object = ctx->object,
      .header = ctx->header,
    };
    announce_package(b, embed.action, unit->pkg);
    sp_ht_insert(b->ids.embeds, unit, embed);
    spn_try(spn_dag_action_add_output(g, embed.action, embed.object));
    spn_try(spn_dag_action_add_output(g, embed.action, embed.header));
    si_da_for(unit->target->embed, it) {
      spn_embed_t* entry = &unit->target->embed[it];
      if (entry->kind == SPN_EMBED_FILE) {
        spn_dag_action_add_input(g, embed.action, spn_dag_add_file(g, entry->path));
      }
    }
  }

  spn_try(add_object_compilation(b, mem, unit, libc, embed.header, needs, &objects));

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
    spn_digest_init(&digest);
    spn_dag_hash_str(&digest, sp_str_lit("spn.build.rsp.v3"));
    spn_dag_hash_u8(&digest, (u8)rsp->style);
    spn_dag_hash_str(&digest, unit->pkg->pkg.manifest->name.str);
    spn_dag_hash_str(&digest, unit->target->info->name.str);

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

  if (!sp_opt_is_null(emit->archive)) {
    si_archive_t* ctx = sp_alloc_type(b->mem, si_archive_t);
    *ctx = (si_archive_t) {
      .plan = &emit->archive.value,
      .file_name = emit->output,
      .subject = spn_target_unit_subject(unit),
      .objects = link_objects,
      .output = ids->output,
    };

    spn_digest_ctx_t digest = sp_zero;
    spn_digest_init(&digest);
    spn_dag_hash_str(&digest, sp_str_lit("spn.build.archive.v3"));
    spn_dag_hash_u64(&digest, toolchain->identity);
    hash_invocation(&digest, g->roots, &emit->archive.value.base);
    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .identity = spn_dag_hash_final(&digest),
      .execute = si_on_archive,
      .user_data = ctx,
    });
    announce_package(b, action, unit->pkg);
    si_da_for(link_inputs, it) {
      spn_dag_action_add_input(g, action, link_inputs[it]);
    }
    spn_try(spn_dag_action_add_output(g, action, ids->output));
  }

  if (!sp_opt_is_null(emit->link)) {
    const spn_cc_link_plan_t* plan = &emit->link.value;
    si_link_t* ctx = sp_alloc_type(b->mem, si_link_t);
    *ctx = (si_link_t) {
      .plan = plan,
      .file_name = emit->output,
      .subject = spn_target_unit_subject(unit),
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

    if (!sp_opt_is_null(emit->exports)) {
      const spn_exports_plan_t* exports = &emit->exports.value;
      si_exports_t* exports_ctx = sp_alloc_type(b->mem, si_exports_t);
      *exports_ctx = (si_exports_t) {
        .archive = &exports->archive,
        .format = exports->format,
        .rsp = exports->rsp,
        .library = exports->library,
        .subject = spn_target_unit_subject(unit),
        .objects = link_objects,
        .whole_archives = ctx->deps.whole_archives,
      };

      spn_digest_ctx_t digest = sp_zero;
      spn_digest_init(&digest);
      spn_dag_hash_cstr(&digest, "spn.build.exports.v7");
      spn_dag_hash_u64(&digest, toolchain->identity);
      spn_dag_hash_s32(&digest, exports->format);
      spn_dag_hash_s32(&digest, exports->rsp);
      spn_dag_hash_str(&digest, exports->library);
      hash_link_deps(&digest, link);
      spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .identity = spn_dag_hash_final(&digest),
        .execute = si_on_write_exports,
        .user_data = exports_ctx,
      });
      announce_package(b, action, unit->pkg);
      si_da_for(link_inputs, it) {
        spn_dag_action_add_input(g, action, link_inputs[it]);
      }
      si_da_for(ctx->deps.whole_archives, it) {
        spn_dag_action_add_input(g, action, ctx->deps.whole_archives[it]);
      }
      ctx->exports = spn_dag_add_output(g, exports->file_name);
      exports_ctx->output = ctx->exports;
      spn_try(spn_dag_action_add_output(g, action, ctx->exports));
      si_da_push(mem, link_inputs, ctx->exports);
    }

    spn_digest_ctx_t digest = sp_zero;
    spn_digest_init(&digest);
    spn_dag_hash_str(&digest, sp_str_lit("spn.build.link.v11"));
    spn_dag_hash_u64(&digest, toolchain->identity);
    hash_invocation(&digest, g->roots, &plan->base);
    spn_dag_hash_args(&digest, g->roots, plan->tail);
    hash_link_deps(&digest, link);
    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .identity = spn_dag_hash_final(&digest),
      .execute = si_on_link,
      .user_data = ctx,
    });
    announce_package(b, action, unit->pkg);
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
        .kind = link->cc.kind,
        .lang = link->cc.lang,
        .system_libs = link->cc.system_libs,
      };
      spn_zig_stub_t stub = spn_zig_stub(b->mem, &build->profile, &stub_link);

      spn_digest_ctx_t warm_digest = sp_zero;
      spn_digest_init(&warm_digest);
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
  }

  if (!spn_path_empty(unit->install)) {
    spn_try(add_publish_file(b, ids->output, spn_path_join(b->mem, unit->install, emit->output)));
    if (ids->implib.occupied) {
      spn_try(add_publish_file(b, ids->implib, spn_path_join(b->mem, unit->install, emit->implib)));
    }
  }

  return SPN_OK;
}

spn_err_t spn_dag_build_lower(spn_dag_build_t* b, const spn_build_plan_t* plan) {
  spn_dag_t* g = b->graph;

  si_da_for_value(plan->pkgs, pkg) {
    si_om_insert(b->mem, b->ids.packages, pkg, sp_zero_s(si_dag_pkg_ids_t));
  }

  si_da_for_value(plan->targets, target) {
    spn_build_unit_t* build = target->pkg->build;
    if (!sp_ht_getp(b->ids.builds, build)) {
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
          spn_digest_init(&digest);
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
    }

    if (sp_str_empty(target->emit.output)) {
      continue;
    }
    spn_dag_target_ids_t ids = {
      .output = spn_dag_add_output(g, target->emit.output),
    };
    if (!sp_str_empty(target->emit.implib)) {
      ids.implib = spn_dag_add_output(g, target->emit.implib);
    }
    sp_ht_insert(b->ids.targets, target, ids);
  }

  si_da_for(plan->publishes, i) {
    spn_publish_plan_t* publish = &plan->publishes[i];
    spn_pkg_unit_t* pkg = publish->pkg;

    spn_digest_ctx_t digest = sp_zero;
    spn_digest_init(&digest);
    spn_dag_hash_str(&digest, sp_str_lit("spn.build.publish.v2"));
    spn_dag_hash_str(&digest, pkg->pkg.manifest->name.str);
    hash_pin(&digest, &pkg->pin);
    si_da_for(publish->entries, it) {
      hash_publish(&digest, g->roots, &publish->entries[it]);
    }

    si_publish_t* ctx = sp_alloc_type(b->mem, si_publish_t);
    *ctx = (si_publish_t) {
      .pkg = pkg->pkg.manifest->name.str,
      .entries = publish->entries,
      .include = spn_dag_add_tree(g, pkg->paths.include),
      .share = spn_dag_add_tree(g, pkg->paths.share),
    };
    si_dag_pkg_ids_t* ids = si_om_get(b->ids.packages, pkg);
    sp_assert(ids);
    ids->include = ctx->include;
    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .kind = SPN_DAG_ACTION_DISCOVERED,
      .identity = spn_dag_hash_final(&digest),
      .execute = si_on_publish,
      .user_data = ctx,
    });
    spn_try(spn_dag_action_add_output(g, action, ctx->include));
    spn_try(spn_dag_action_add_output(g, action, ctx->share));
  }

  si_da_for_value(plan->nodes, node) {
    spn_pkg_unit_t* pkg = node->unit;
    spn_dag_id_t script = sp_zero;
    if (!sp_str_empty(node->fn)) {
      spn_dag_target_ids_t* ids = sp_ht_getp(b->ids.targets, pkg->metaprogram);
      sp_assert(ids);
      script = ids->output;
    }

    spn_digest_ctx_t digest = sp_zero;
    spn_digest_init(&digest);
    spn_dag_hash_str(&digest, sp_str_lit("spn.build.user.v9"));
    spn_dag_hash_str(&digest, pkg->pkg.manifest->name.str);
    spn_dag_hash_u64(&digest, pkg->fingerprint);
    hash_pin(&digest, &pkg->pin);
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
    announce_package(b, action, pkg);

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
    si_dag_pkg_ids_t* ids = si_om_get(b->ids.packages, pkg);
    sp_assert(ids);
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
      si_da_push(b->mem, ids->outputs, output);
    }
  }

  si_da_for_value(plan->targets, target) {
    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    spn_err_t err = add_target(b, scratch.mem, target);
    sp_mem_end_scratch(scratch);
    spn_try(err);
  }

  sp_ht_for_kv(b->ids.embeds, it) {
    spn_target_unit_t* target = *it.key;
    si_da_for(target->target->embed, et) {
      spn_embed_t* embed = &target->target->embed[et];
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

  si_da_for_value(plan->claims, claim) {
    spn_dag_add_claim(g, claim);
  }
  si_da_for_value(plan->stages, stage) {
    si_da_push(b->mem, b->stages.checkout, ((spn_dag_stage_t) {
      .key = stage.to.sub,
      .to = stage.to,
      .artifact = spn_dag_add_path(g, stage.from->path, stage.from->kind),
    }));
  }
  si_da_for_value(plan->closures, closure) {
    sp_str_t key = closure.exe.target->target->info->name.str;
    si_da_push(b->mem, b->stages.profile, ((spn_dag_stage_t) {
      .key = key,
      .to = closure.exe.path,
      .artifact = sp_ht_getp(b->ids.targets, closure.exe.target)->output,
    }));
    si_da_for_value(closure.libs, lib) {
      si_da_push(b->mem, b->stages.profile, ((spn_dag_stage_t) {
        .key = key,
        .to = lib.path,
        .artifact = sp_ht_getp(b->ids.targets, lib.target)->output,
      }));
    }
  }

  return SPN_OK;
}

/////////
// RUN //
/////////
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

  b->run = (spn_dag_run_t) {
    .g = b->graph,
    .mem = b->mem,
    .files = &session->dag.files,
    .cache = &session->dag.actions,
    .store = &session->dag.store,
    .discovery = &session->dag.discovery,
    .scratch = session->dag.scratch,
    .trace = dag_trace,
    .trace_data = b,
    .cancel = &op->cancelled,
    .wake = &op->ctx->wake,
  };

  return b;
}

spn_err_t spn_dag_build_run(spn_dag_build_t* b, u32 workers) {
  spn_thread_pool_t pool = sp_zero;
  spn_thread_pool_init(&pool, spn.mem, (spn_thread_pool_config_t) {
    .workers = sp_min(workers, (u32)sp_da_size(b->graph->actions)),
    .on_worker_exit = spn_wasm_thread_exit,
  });

  spn_dag_file_cache_invalidate_all(b->run.files);
  spn_err_t result = spn_dag_run(&b->run, &pool);
  spn_thread_pool_deinit(&pool);
  spn_dag_file_cache_flush(b->run.files, b->session->dag.files_path);

  switch (result) {
    case SPN_OK:
    case SPN_ERR_DAG_CANCELLED:
    case SPN_ERR_DAG_ACTION: {
      return result;
    }
    default: {
      break;
    }
  }

  spn_dag_diag_t* diag = &b->run.diag;
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
