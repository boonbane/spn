#include "spn/host.h"

#include "core/types.h"
#include "ctx/types.h"
#include "error/error.h"
#include "event/event.h"
#include "graph/build.h"
#include "op/op.h"
#include "paths/paths.h"
#include "session/session.h"
#include "session/types.h"
#include "unit/types.h"

static spn_err_t run_test(spn_session_t* session, spn_target_unit_t* unit, bool* passed) {
  spn_ctx_t* ctx = session->ctx;
  spn_path_t staged = spn_target_unit_staged_path(session->mem, unit);
  sp_str_t command = spn_path_str(&ctx->roots, session->mem, staged);

  spn_event_buffer_push(ctx->events, (spn_event_t) {
    .kind = SPN_EVENT_TARGET_RUN,
    .pkg = unit->pkg->info->name,
    .target_run = {
      .name = unit->info->name,
      .command = command,
    },
  });

  if (!sp_fs_exists_at(spn_path_at(&ctx->roots, staged))) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_TEST_MISSING,
      .script = { .name = unit->info->name, .path = command },
    });
  }

  sp_tm_timer_t timer = sp_tm_start_timer();
  sp_ps_output_t output = sp_ps_run(session->mem, (sp_ps_config_t) {
    .command = command,
    .cwd = spn_path_str(&ctx->roots, session->mem, unit->pkg->paths.roots.source),
    .io = {
      .in =  { .mode = SP_PS_IO_MODE_NULL },
      .out = { .mode = SP_PS_IO_MODE_CREATE },
      .err = { .mode = SP_PS_IO_MODE_CREATE },
    },
  });
  u64 time = sp_tm_read_timer(&timer);

  *passed = output.status.exit_code == 0;
  if (*passed) {
    spn_event_buffer_push(ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_TEST_PASSED,
      .test_passed = {
        .name = unit->info->name,
        .time = time,
      },
    });
  }
  else {
    spn_event_buffer_push(ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_TEST_FAILED,
      .test_failed = {
        .name = unit->info->name,
        .code = output.status.exit_code,
        .out = output.out,
        .err = output.err,
        .time = time,
      },
    });
  }

  return SPN_OK;
}

spn_err_t spn_op_test(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_ctx_t* ctx = session->ctx;
  sp_tm_timer_t timer = sp_tm_start_timer();

  spn_test_result_t* result = &op->result.test;

  sp_da_for(session->plans.build, i) {
    spn_build_plan_t* plan = &session->plans.build[i];
    si_da_for(plan->roots, j) {
      spn_target_unit_t* unit = spn_session_find_target_in_pkg(session, plan->root, plan->roots[j]);
      if (unit->info->kind != SPN_TARGET_KIND_TEST) {
        continue;
      }
      bool test_passed = sp_zero;
      spn_try(run_test(session, unit, &test_passed));
      if (test_passed) {
        result->passed++;
      }
      else {
        result->failed++;
      }
    }
  }

  spn_event_buffer_push(ctx->events, (spn_event_t) {
    .kind = SPN_EVENT_TEST_SUMMARY,
    .test_summary = {
      .passed = result->passed,
      .failed = result->failed,
      .time = sp_tm_read_timer(&timer),
    },
  });

  if (result->failed) {
    return SPN_ERR_TEST_FAILED;
  }
  return SPN_OK;
}

spn_op_t* spn_run_tests(spn_session_t* session) {
  spn_op_t* op = spn_op_new(session->ctx, session, SPN_OP_TEST);
  spn_op_submit(op);
  return op;
}
