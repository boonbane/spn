#include "ctx/types.h"
#include "core/core.h"
#include "event/event.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "external/zig.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "graph/nodes/nodes.h"

typedef struct {
  sp_io_writer_t base;
  spn_zig_progress_t progress;
  spn_dag_env_t* env;
  u64 last;
} progress_writer_t;

static sp_err_t progress_write(sp_io_writer_t* writer, const void* bytes, u64 len, u64* written) {
  progress_writer_t* w = (progress_writer_t*)writer;
  *written = len;
  if (!spn_zig_progress_feed(&w->progress, bytes, len) || !w->env->progress) {
    return SP_OK;
  }
  u64 ticks = spn_zig_progress_ticks(&w->progress);
  sp_atomic_u64_add(&w->env->progress->warm, ticks - w->last, SP_ATOMIC_SEQ_CST);
  w->last = ticks;
  if (w->env->wake) {
    spn_wake_ring(w->env->wake);
  }
  return SP_OK;
}

spn_err_t spn_dag_exec_warm(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_dag_warm_ctx_t* warm = (spn_dag_warm_ctx_t*)user_data;
  spn_cc_t* cc = &warm->build->toolchain->cc;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_mem_t mem = scratch.mem;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_WARM_START,
    .warm = {
      .toolchain = cc->name,
      .triple = warm->triple,
      .stub = warm->name,
    },
  });

  spn_path_t staging = spn_path_parent(outputs[0]);
  sp_da(spn_arg_t) objects = sp_da_new(mem, spn_arg_t);
  sp_da_push(objects, spn_arg_path(spn_path(mem, SPN_DIR_ID_RUNTIME, "zig/stub.c")));

  spn_invocation_t invocation = sp_zero;
  spn_gnu_render_link(mem, cc, &warm->build->profile, &warm->link, objects, spn_path_join(mem, staging, sp_str_lit("stub.bin")), sp_zero_struct(spn_path_t), &invocation);
  invocation.cwd = staging;

  progress_writer_t* progress = sp_alloc_type(mem, progress_writer_t);
  *progress = (progress_writer_t) { .base = { .write = progress_write }, .env = env };
  spn_invocation_result_t run = spn_invocation_run_progress(g->roots, &invocation, spn_path_join(mem, staging, sp_str_lit("log")), &progress->base);
  if (env->progress && progress->last) {
    sp_atomic_u64_add(&env->progress->warm, 0 - progress->last, SP_ATOMIC_SEQ_CST);
    if (env->wake) {
      spn_wake_ring(env->wake);
    }
  }

  spn_err_t err = SPN_OK;
  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_WARM_FAILED,
      .warm_failed = {
        .toolchain = cc->name,
        .triple = warm->triple,
        .stub = warm->name,
        .rc = run.result.status.exit_code,
        .command = spn_invocation_to_str(g->roots, spn.mem, &invocation),
        .out = run.result.out,
      },
    });
    err = SPN_ERR_DAG_ACTION;
  }
  else if (sp_fs_create_file_at(spn_path_at(g->roots, outputs[0]))) {
    err = SPN_ERR_DAG_OUTPUT_WRITE;
  }

  sp_mem_end_scratch(scratch);
  return err;
}
