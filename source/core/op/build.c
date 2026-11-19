#include "core/types.h"
#include "spn/host.h"

#include "cpu/cpu.h"
#include "ctx/types.h"
#include "dag/dag.h"
#include "dag/run.h"
#include "error/error.h"
#include "event/event.h"
#include "fs/fs.h"
#include "external/wasm/wasm.h"
#include "graph/dag.h"
#include "model/sync.h"
#include "op/op.h"
#include "paths/paths.h"
#include "pkg/pkg.h"
#include "profile/types.h"
#include "project/types.h"
#include "session/session.h"
#include "session/types.h"
#include "stage/stage.h"
#include "triple/triple.h"
#include "unit/types.h"
#include "unit/unit.h"

static void claim_spn_files(sp_mem_t mem, spn_project_t* project, spn_build_plan_t* plan) {
  si_da_push(mem, plan->claims, ((spn_dag_claim_t) { .path = spn_path_project_compile_commands(), .kind = SPN_DAG_ARTIFACT_KIND_FILE }));
  si_da_push(mem, plan->claims, ((spn_dag_claim_t) { .path = project->paths.lock, .kind = SPN_DAG_ARTIFACT_KIND_FILE }));
  si_da_push(mem, plan->claims, ((spn_dag_claim_t) { .path = spn_path_project_staged(), .kind = SPN_DAG_ARTIFACT_KIND_FILE }));
}

static spn_err_t stage_rows(sp_mem_t scratch, spn_dag_build_t* b, si_da(spn_dag_stage_t) stages, si_da(spn_stage_t)* rows) {
  si_da_for_ptr(stages, stage) {
    const spn_dag_artifact_state_t* state = spn_dag_run_artifact(&b->run, stage->artifact);
    switch (spn_dag_find_artifact(b->graph, stage->artifact)->kind) {
      case SPN_DAG_ARTIFACT_KIND_FILE: {
        si_da_push(scratch, *rows, ((spn_stage_t) { .key = stage->key, .to = stage->to, .digest = state->digest, .location = state->location }));
        break;
      }
      case SPN_DAG_ARTIFACT_KIND_TREE: {
        sp_da(spn_dag_action_output_t) entries = sp_zero;
        spn_err_t listed = spn_dag_tree_entries(b->run.store, state->digest, scratch, &entries);
        if (listed) {
          return spn_err_emit(b->session->ctx, (spn_err_union_t) {
            .kind = listed,
            .dag = { .path = spn_path_str(b->graph->roots, b->session->ctx->events->mem, stage->to) },
          });
        }
        sp_da_for(entries, et) {
          si_da_push(scratch, *rows, ((spn_stage_t) {
            .key = stage->key,
            .to = spn_path_join(scratch, stage->to, entries[et].name),
            .digest = entries[et].digest,
            .location = spn_path_join(scratch, state->location, entries[et].name),
          }));
        }
        break;
      }
      case SPN_DAG_ARTIFACT_KIND_VALUE: {
        sp_unreachable_case();
      }
    }
  }
  return SPN_OK;
}

static spn_err_t stage(sp_mem_t scratch, spn_dag_build_t* b, spn_pkg_unit_t* root) {
  spn_session_t* session = b->session;
  const spn_path_roots_t* roots = b->graph->roots;

  si_da(sp_str_t) targets = sp_zero;
  si_om_for_each(root->pkg.targets, target) {
    si_da_push(scratch, targets, target->info->name.str);
  }
  si_da(sp_str_t) copies = sp_zero;
  si_da_for_value(session->manifest->stage, copy) {
    si_da_push(scratch, copies, copy.to);
  }

  si_da(spn_stage_t) profile = sp_zero;
  si_da(spn_stage_t) checkout = sp_zero;
  spn_try(stage_rows(scratch, b, b->stages.profile, &profile));
  spn_try(stage_rows(scratch, b, b->stages.checkout, &checkout));

  spn_path_t manifest = spn_path_join(scratch, session->builds.program->paths.root, sp_str_lit(".spn/staged"));
  spn_stage_diag_t diag = spn_stage_apply(scratch, roots, b->run.files, manifest, targets, profile);
  if (!diag.err) {
    diag = spn_stage_apply(scratch, roots, b->run.files, spn_path_project_staged(), copies, checkout);
  }
  if (diag.err) {
    return spn_err_emit(session->ctx, (spn_err_union_t) { .kind = diag.err, .fs = { .path = diag.path } });
  }
  return SPN_OK;
}

static spn_err_t emit_warnings(spn_dag_build_t* b, u32* warned) {
  spn_ctx_t* ctx = b->session->ctx;
  const spn_path_roots_t* roots = b->graph->roots;
  spn_dag_digest_t silent = spn_dag_digest("", 0);

  si_da_for_ptr(b->logs, log) {
    const spn_dag_artifact_state_t* state = spn_dag_run_artifact(&b->run, log->artifact);
    if (spn_dag_digest_equal(state->digest, silent)) {
      continue;
    }

    sp_str_t output = sp_zero;
    if (sp_fs_read_at(ctx->events->mem, spn_path_at(roots, state->location), &output)) {
      return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_READ, .fs = { .path = state->location } });
    }

    switch (log->kind) {
      case SPN_DAG_LOG_COMPILE: {
        spn_event_buffer_push(ctx->events, (spn_event_t) {
          .kind = SPN_EVENT_TARGET_BUILD_WARNED,
          .target_warned = { .subject = log->subject, .source_file = log->source, .output = output },
        });
        break;
      }
      case SPN_DAG_LOG_LINK: {
        spn_event_buffer_push(ctx->events, (spn_event_t) {
          .kind = SPN_EVENT_LINK_WARNED,
          .link_warned = { .subject = log->subject, .file_name = log->file_name, .output = output },
        });
        break;
      }
    }
    (*warned)++;
  }
  return SPN_OK;
}

spn_err_t spn_op_build(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_ctx_t* ctx = session->ctx;
  sp_mem_t mem = session->mem;
  spn_project_t* project = session->project;
  const spn_path_roots_t* roots = &ctx->roots;

  spn_path_t log_path = spn_path_join(mem, session->builds.program->paths.root, sp_str_lit(".spn/build.jsonl"));
  if (spn_event_log_open(ctx->events, spn_path_at(roots, log_path))) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_FS_WRITE,
      .fs = { .path = log_path },
    });
  }

  spn_try(spn_resolve_and_sync(op));

  // Initialize shared state across the DAGs
  session->dag.files_path = spn_path_join(session->mem, session->paths.build, sp_str_lit(".spn/files"));
  spn_dag_file_cache_init(&session->dag.files, session->mem, roots);
  spn_dag_file_cache_load(&session->dag.files, session->dag.files_path);

  spn_dag_layout_t layout = spn_dag_layout(session->mem, spn_path_anchor(session->mem, roots, spn_path_from_id(SPN_DIR_ID_DAG)));
  session->dag.scratch = layout.scratch;
  sp_fs_create_dir_at(spn_path_at(roots, session->dag.scratch));
  spn_dag_store_init(&session->dag.store, (spn_dag_store_config_t) {
    .kind = SPN_DAG_STORE_FILESYSTEM,
    .mem = spn.mem,
    .roots = roots,
    .dir = layout.store,
  });
  spn_dag_action_cache_init(&session->dag.actions, spn.mem, roots, layout.strong);
  spn_dag_obs_table_init(&session->dag.discovery, spn.mem, roots, layout.weak);
  session->dag.files.stats = &session->dag.stats;
  session->dag.actions.stats = &session->dag.stats;
  session->dag.discovery.stats = &session->dag.stats;
  session->dag.store.stats = &session->dag.stats;

  spn_build_unit_t* build = session->builds.program;

  spn_units_t configure = {
    .ctx = ctx,
    .mem = mem,
    .resolve = session->resolve,
    .metabuild = session->builds.metaprogram,
  };
  spn_units_t units = {
    .ctx = ctx,
    .mem = mem,
    .resolve = session->resolve,
    .metabuild = session->builds.metaprogram,
    .configure = &configure,
  };

  spn_pkg_unit_t* root = SP_NULLPTR;
  spn_try(si_add_package_unit(&units, build, session->root, &root));
  si_da(spn_target_unit_t*) targets = sp_zero;
  si_om_for_each(root->pkg.targets, target) {
    if (!(session->selection.kinds & spn_target_kind_bit(target->info->kind))) {
      continue;
    }
    if (!si_om_empty(session->selection.names) && !si_om_has(session->selection.names, target->info->name.id)) {
      continue;
    }

    spn_target_unit_t* unit = sp_zero;
    spn_try(si_add_target_unit(&units, root, target, &unit));
    si_da_push(mem, targets, unit);
  }

  // If a name was explicitly asked for, we don't want to silently proceed if
  // it didn't match anything (e.g. if the user typo'd), even if other parts
  // of the selection did match something
  si_om_for_each(session->selection.names, name) {
    bool matched = false;
    si_da_for(targets, it) {
      if (targets[it]->id.target.name == name->id) {
        matched = true;
        break;
      }
    }
    if (!matched) {
      return spn_err_emit(ctx, (spn_err_union_t) {
        .kind = SPN_ERR_TARGET_SELECTION,
        .target = { .name = name->str },
      });
    }
  }

  spn_build_plan_t plan = sp_zero;
  spn_try(spn_plan_new(&configure, SP_NULLPTR, SP_NULLPTR, &plan));
  claim_spn_files(mem, project, &plan);
  spn_dag_build_t* reactors = spn_dag_build_new(op);
  spn_try(spn_dag_build_lower(reactors, &plan));
  spn_try(spn_dag_build_run(reactors, spn_cpu_count()));
  sp_om_for(units.packages, it) {
    spn_pkg_unit_t* pkg = sp_om_at(units.packages, it);
    if (!pkg->reactor) {
      continue;
    }
    spn_dag_target_ids_t* ids = sp_ht_getp(reactors->ids.targets, pkg->reactor);
    sp_assert(ids);
    spn_try(spn_wasm_configure(pkg, spn_dag_run_artifact(&reactors->run, ids->link)->location));
  }

  spn_try(spn_plan_new(&units, root, targets, &session->plan));
  claim_spn_files(mem, project, &session->plan);

  spn_dag_build_t* b = spn_dag_build_new(op);
  spn_try(spn_dag_build_lower(b, &session->plan));

  spn_event_buffer_push(ctx->events, (spn_event_t) {
    .kind = SPN_EVENT_INIT_BUILD_GRAPH,
    .pkg = session->manifest->name.str,
    .graph_init = {
      .profile = build->profile.name.str,
      .target = spn_triple_to_str(ctx->events->mem, build->profile.target),
      .toolchain = build->toolchain->info->name.str,
      .version = build->toolchain->version,
      .force = session->force,
    }
  });

  sp_tm_timer_t timer = sp_tm_start_timer();
  sp_atomic_ptr_store(&ctx->progress, &b->run.progress, SP_ATOMIC_SEQ_CST);
  spn_err_t result = spn_dag_build_run(b, spn_cpu_count());
  sp_atomic_ptr_store(&ctx->progress, SP_NULLPTR, SP_ATOMIC_SEQ_CST);
  u64 elapsed = sp_tm_read_timer(&timer);

  if (result == SPN_ERR_DAG_CANCELLED) {
    return result;
  }

  spn_dag_lowered_t lowered [] = {
    { .build = reactors, .plan = &plan },
    { .build = b, .plan = &session->plan },
  };
  spn_err_t written = spn_dag_build_write_compile_commands(session, lowered, sp_carr_len(lowered));
  if (!result) {
    result = written;
  }
  if (!result) {
    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    result = stage(scratch.mem, b, root);
    sp_mem_end_scratch(scratch);
    spn_dag_file_cache_flush(b->run.files, session->dag.files_path);
  }
  u32 warned = 0;
  if (!result) {
    result = emit_warnings(reactors, &warned);
  }
  if (!result) {
    result = emit_warnings(b, &warned);
  }

  bool failed = result != SPN_OK;
  u32 hits = (u32)sp_atomic_s32_load(&b->run.progress.hits, SP_ATOMIC_SEQ_CST);
  u32 misses = (u32)sp_atomic_s32_load(&b->run.progress.misses, SP_ATOMIC_SEQ_CST);
  const si_pkg_info_t* pkg = session->manifest;
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
        .warned = warned,
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
      .total = (u32)sp_da_size(b->graph->actions),
      .time = elapsed,
      .profile = profile->name.str,
      .hashed_files = sp_atomic_u32_load(&session->dag.stats.hashed_files, SP_ATOMIC_SEQ_CST),
      .hashed_bytes = sp_atomic_u64_load(&session->dag.stats.hashed_bytes, SP_ATOMIC_SEQ_CST),
      .stats = sp_atomic_u32_load(&session->dag.stats.stats, SP_ATOMIC_SEQ_CST),
      .obs_rows = sp_atomic_u32_load(&session->dag.stats.obs_rows, SP_ATOMIC_SEQ_CST),
      .cache_reads = sp_atomic_u32_load(&session->dag.stats.cache_reads, SP_ATOMIC_SEQ_CST),
      .cache_writes = sp_atomic_u32_load(&session->dag.stats.cache_writes, SP_ATOMIC_SEQ_CST),
    },
  });

  spn_cache_config_t cache = ctx->config.cache;
  sp_path_t gate = spn_path_at(roots, spn_path_lock(session->mem, spn_path_from_id(SPN_DIR_ID_DAG)));
  sp_tm_epoch_t now = sp_tm_now_epoch();
  sp_sys_file_meta_t swept = sp_zero;
  bool due = sp_fs_get_path_metadata_at(gate, &swept) || now.s - (u64)swept.mtime.tv_sec >= cache.sweep_interval_s;
  sp_fs_lock_t lock = sp_zero;
  bool acquired = false;
  if (cache.max_bytes && due && !sp_fs_create_parent_at(gate) && !sp_fs_lock_try_acquire(&lock, gate, &acquired) && acquired) {
    sp_sys_timespec_t time = { .tv_sec = (s64)now.s, .tv_nsec = now.ns };
    sp_sys_set_times_s(gate.dir, gate.sub, time, time);

    sp_tm_timer_t sweep_timer = sp_tm_start_timer();
    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    spn_dag_sweep_t sweep = spn_dag_sweep(scratch.mem, roots, &layout, (spn_dag_sweep_limits_t) {
      .max_bytes = cache.max_bytes,
      .keep_bytes = cache.max_bytes / 5 * 4,
      .min_age_s = spn_dag_sweep_min_age(cache.min_age_s),
    });
    sp_mem_end_scratch(scratch);
    sp_fs_lock_release(&lock);

    spn_event_buffer_push(ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_CACHE_SWEEP,
      .sweep = {
        .size = sweep.bytes,
        .freed = sweep.freed,
        .removed = sweep.removed,
        .time = sp_tm_read_timer(&sweep_timer),
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
