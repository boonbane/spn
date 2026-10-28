#include "spn/host.h"

#include "cpu/cpu.h"
#include "dag/dag.h"
#include "error/error.h"
#include "event/event.h"
#include "external/wasm/wasm.h"
#include "git/patch.h"
#include "graph/dag.h"
#include "graph/nodes/nodes.h"
#include "model/resolve.h"
#include "model/sync.h"
#include "op/op.h"
#include "paths/paths.h"
#include "profile/types.h"
#include "project/project.h"
#include "project/types.h"
#include "session/types.h"
#include "triple/triple.h"
#include "unit/types.h"
#include "unit/unit.h"

spn_err_t spn_op_build(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_ctx_t* ctx = session->ctx;
  spn_project_t* project = session->project;
  const spn_path_roots_t* roots = &ctx->roots;

  spn_err_t hashed = SPN_OK;
  si_da_for_ptr(project->patches, decl) {
    spn_patch_t patch = { .qualified = decl->qualified };
    si_da_for_value(decl->files, file) {
      si_da_push(session->mem, patch.set.files, spn_path_str(roots, session->mem, file));
    }
    u32 missing = 0;
    patch.set.hash = spn_git_patch_hash(patch.set.files, &missing);
    if (!patch.set.hash) {
      spn_err_t kind = spn_err_emit(ctx, (spn_err_union_t) {
        .kind = SPN_ERR_PATCH_FILE_MISSING,
        .subject = decl->qualified.str,
        .patch_file = { .path = patch.set.files[missing] },
      });
      if (!hashed) {
        hashed = kind;
      }
      continue;
    }
    si_da_push(session->mem, session->patches, patch);
  }
  spn_try(hashed);

  bool reresolve = sp_zero;
  do {
    spn_try(spn_resolve(op));
    spn_try(spn_sync(op, &reresolve));
  } while (reresolve);

  spn_err_t unused = SPN_OK;
  si_da_for_ptr(session->patches, patch) {
    bool used = false;
    sp_ht_for_kv(session->resolve, it) {
      if (it.key->qualified == patch->qualified.id) {
        used = true;
        break;
      }
    }
    if (used) {
      continue;
    }
    spn_err_t kind = spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_PATCH_UNUSED,
      .subject = patch->qualified.str,
    });
    if (!unused) {
      unused = kind;
    }
  }
  spn_try(unused);

  session->dag.files_path = spn_path_join(session->mem, session->paths.build, sp_str_lit(".spn/files"));
  spn_dag_file_cache_init(&session->dag.files, session->mem, roots);
  spn_dag_file_cache_load(&session->dag.files, session->dag.files_path);

  if (spn_wasm_init()) {
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_WASM_INIT_FAILED });
  }
  spn_try(spn_units_add_packages(session));
  spn_try(spn_units_add_targets(session, SPN_UNIT_SCOPE_METAPROGRAM));

  spn_dag_build_t* configure = spn_dag_build_new(op);
  spn_try(spn_dag_build_add_build(configure, session->units.metaprogram));
  sp_da_for(session->units.metaprogram->packages, it) {
    spn_target_unit_t* reactor = session->units.metaprogram->packages[it]->scripts.configure;
    if (!reactor) {
      continue;
    }
    spn_dag_build_declare_target(configure, reactor);
  }
  sp_da_for(session->units.metaprogram->packages, it) {
    spn_target_unit_t* reactor = session->units.metaprogram->packages[it]->scripts.configure;
    if (!reactor) {
      continue;
    }
    spn_try(spn_dag_build_add_target(configure, reactor));
  }
  sp_da_for(session->plans.build, i) {
    sp_da_for(session->plans.build[i].build->packages, j) {
      spn_pkg_unit_t* unit = session->plans.build[i].build->packages[j];
      spn_target_unit_t* reactor = unit->metaprogram ? unit->metaprogram->scripts.configure : SP_NULLPTR;
      if (!reactor) {
        continue;
      }
      spn_dag_target_ids_t* ids = sp_ht_getp(configure->ids.targets, reactor);
      sp_assert(ids);
      si_configure_t* action = sp_alloc_type(configure->mem, si_configure_t);
      *action = (si_configure_t) {
        .unit = unit,
        .reactor = ids->output,
      };
      spn_dag_id_t id = spn_dag_add_action(configure->graph, (spn_dag_action_config_t) {
        .kind = SPN_DAG_ACTION_UNCACHEABLE,
        .execute = si_on_configure,
        .user_data = action,
      });
      spn_dag_action_add_input(configure->graph, id, ids->output);
    }
  }
  spn_try(spn_dag_build_run(configure, spn_cpu_count()));

  spn_try(spn_units_add_targets(session, SPN_UNIT_SCOPE_TARGET));

  spn_dag_build_t* dag = spn_dag_build_new(op);
  spn_try(spn_dag_build_add_session(dag));

  spn_build_unit_t* unit = session->units.target;
  spn_event_buffer_push(ctx->events, (spn_event_t) {
    .kind = SPN_EVENT_INIT_BUILD_GRAPH,
    .pkg = session->manifest->name.str,
    .graph_init = {
      .profile = unit->profile.name.str,
      .target = spn_triple_to_str(ctx->events->mem, spn_profile_triple(&unit->profile)),
      .toolchain = unit->toolchain->info->name.str,
      .version = unit->toolchain->version,
      .force = session->force,
    }
  });

  sp_atomic_ptr_store(&ctx->progress, &dag->run.progress, SP_ATOMIC_SEQ_CST);
  spn_err_t result = spn_dag_build_run(dag, spn_cpu_count());
  sp_atomic_ptr_store(&ctx->progress, SP_NULLPTR, SP_ATOMIC_SEQ_CST);
  u64 elapsed = sp_tm_read_timer(&dag->timer);

  if (result == SPN_ERR_DAG_CANCELLED) {
    return result;
  }

  spn_err_t written = spn_dag_build_write_compile_commands(dag);
  if (!result) {
    result = written;
  }
  if (!result) {
    if (!sp_fs_exists_at(spn_path_at(roots, project->paths.lock))) {
      spn_try(spn_project_update_lock(ctx, project, session->resolve));
    }
    result = spn_dag_build_stage(dag);
    spn_dag_file_cache_flush(dag->run.files, session->dag.files_path);
  }

  bool failed = result != SPN_OK;
  u32 hits = (u32)sp_atomic_s32_load(&dag->run.progress.hits, SP_ATOMIC_SEQ_CST);
  u32 misses = (u32)sp_atomic_s32_load(&dag->run.progress.misses, SP_ATOMIC_SEQ_CST);

  sp_da_for(session->plans.build, it) {
    spn_build_plan_t* plan = &session->plans.build[it];
    spn_build_unit_t* build = plan->build;
    const si_pkg_info_t* pkg = plan->root->eval.manifest;
    spn_profile_info_t* profile = &build->profile;

    if (failed) {
      spn_event_buffer_push(ctx->events, (spn_event_t) {
        .kind = SPN_EVENT_BUILD_FAILED,
        .pkg = pkg->name.str,
        .build_failed = {
          .profile = profile->name.str,
          .time = elapsed,
        },
      });
    }
    else {
      spn_event_buffer_push(ctx->events, (spn_event_t) {
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

    spn_event_buffer_push(ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_BUILD_SUMMARY,
      .pkg = pkg->name.str,
      .build_summary = {
        .success = !failed,
        .hits = hits,
        .misses = misses,
        .total = (u32)sp_da_size(dag->graph->actions),
        .time = elapsed,
        .profile = profile->name.str,
        .hashed_files = sp_atomic_u32_load(&dag->stats.hashed_files, SP_ATOMIC_SEQ_CST),
        .hashed_bytes = sp_atomic_u64_load(&dag->stats.hashed_bytes, SP_ATOMIC_SEQ_CST),
        .stats = sp_atomic_u32_load(&dag->stats.stats, SP_ATOMIC_SEQ_CST),
        .obs_rows = sp_atomic_u32_load(&dag->stats.obs_rows, SP_ATOMIC_SEQ_CST),
        .cache_reads = sp_atomic_u32_load(&dag->stats.cache_reads, SP_ATOMIC_SEQ_CST),
        .cache_writes = sp_atomic_u32_load(&dag->stats.cache_writes, SP_ATOMIC_SEQ_CST),
      },
    });
  }

  return result;
}

spn_op_t* spn_build(spn_session_t* session) {
  spn_op_t* op = spn_op_new(session->ctx, session, SPN_OP_BUILD);
  spn_op_submit(op);
  return op;
}
