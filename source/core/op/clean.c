#include "ctx/types.h"
#include "error/error.h"
#include "session/types.h"

#include "spn/host.h"
#include "ctx/ctx.h"
#include "dag/dag.h"
#include "event/event.h"
#include "op/op.h"
#include "paths/paths.h"
#include "profile/profile.h"
#include "stage/stage.h"
#include "os/os.h"

static spn_err_t remove_path(spn_ctx_t* ctx, spn_path_t path) {
  if (sp_fs_remove(spn_path_at(&ctx->roots, path)) != SP_OK) {
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_REMOVE, .fs = { .path = path } });
  }

  return SPN_OK;
}

spn_err_t spn_op_clean(spn_op_t* op) {
  spn_ctx_t* ctx = op->ctx;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_stage_diag_t staged = spn_stage_clean(scratch.mem, &ctx->roots, spn_path_project_staged());
  sp_mem_end_scratch(scratch);
  if (staged.err) {
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = staged.err, .fs = { .path = staged.path } });
  }
  spn_try(remove_path(ctx, spn_path_project_compile_commands()));
  return remove_path(ctx, spn_path_project_build());
}

spn_op_t* spn_clean(spn_ctx_t* ctx) {
  spn_op_t* op = spn_op_new(ctx, SP_NULLPTR, SPN_OP_CLEAN);
  spn_op_submit(op);
  return op;
}

spn_err_t spn_op_clean_cache(spn_op_t* op) {
  spn_ctx_t* ctx = op->ctx;
  u64 min_age = ctx->config.cache.min_age_s;
  if (op->request.clean.min_age_seconds) {
    min_age = sp_max(op->request.clean.min_age_seconds, spn_cache_least_min_age_s);
  }

  sp_tm_timer_t timer = sp_tm_start_timer();
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_dag_layout_t layout = spn_dag_layout(scratch.mem, spn_path_from_id(SPN_DIR_ID_DAG));
  spn_dag_sweep_t sweep = spn_dag_sweep(scratch.mem, &ctx->roots, &layout, (spn_dag_sweep_limits_t) {
    .min_age_s = spn_dag_sweep_min_age(min_age),
  });
  sp_mem_end_scratch(scratch);

  spn_event_buffer_push(ctx->events, (spn_event_t) {
    .kind = SPN_EVENT_CACHE_CLEAN,
    .sweep = {
      .size = sweep.bytes,
      .freed = sweep.freed,
      .removed = sweep.removed,
      .time = sp_tm_read_timer(&timer),
    },
  });
  return SPN_OK;
}

spn_op_t* spn_clean_cache(spn_ctx_t* ctx, spn_clean_request_t request) {
  spn_op_t* op = spn_op_new(ctx, SP_NULLPTR, SPN_OP_CLEAN_CACHE);
  op->request.clean = request;
  spn_op_submit(op);
  return op;
}

spn_err_t spn_op_clean_profile(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_path_t dir = spn_path_join(session->mem, session->paths.build, spn_profile_build_dir(session->mem, &session->builds.program->profile));
  return remove_path(session->ctx, dir);
}

spn_op_t* spn_clean_profile(spn_session_t* session) {
  spn_op_t* op = spn_op_new(session->ctx, session, SPN_OP_CLEAN_PROFILE);
  spn_op_submit(op);
  return op;
}
