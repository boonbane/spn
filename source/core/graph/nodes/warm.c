#include "ctx/types.h"
#include "core/core.h"
#include "event/event.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "external/zig.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "triple/triple.h"
#include "graph/build.h"
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

spn_err_t si_on_zig_warmup(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  si_zig_warmup_t* warm = (si_zig_warmup_t*)user_data;
  const spn_cc_t* cc = warm->cc;
  const spn_profile_info_t* profile = warm->profile;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_mem_t mem = scratch.mem;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_WARM_START,
    .warm = {
      .toolchain = cc->name.str,
      .triple = warm->triple,
      .stub = warm->name,
    },
  });

  spn_path_t staging = spn_path_parent(outputs[0]);
  spn_cc_link_files_t files = {
    .output = spn_path_join(mem, staging, sp_str_lit("stub.bin")),
  };
  if (warm->libc.occupied) {
    files.libc = si_dag_get_artifact_path(g, warm->libc);
  }
  switch (warm->link.kind) {
    case SPN_CC_OUTPUT_SHARED_LIB:
    case SPN_CC_OUTPUT_REACTOR: {
      sp_str_t extension = spn_cc_exports_extension(spn_cc_exports_format(warm->link.kind, spn_os_to_native_object_format(profile->os)));
      files.exports = spn_path_join(mem, staging, sp_fmt(mem, "stub.{}", sp_fmt_str(extension)).value);
      sp_io_file_writer_t writer = sp_zero;
      if (sp_io_file_writer_from_path_at(&writer, spn_path_at(g->roots, files.exports))) {
        sp_mem_end_scratch(scratch);
        return SPN_ERR_DAG_OUTPUT_WRITE;
      }
      spn_cc_render_exports(&writer.base, g->roots, cc, profile, warm->link.kind, sp_str_lit("stub"), SP_NULLPTR);
      sp_io_file_writer_close(&writer);
      if (spn_cc_link_implib(profile, warm->link.kind)) {
        files.implib = spn_path_join(mem, staging, sp_str_lit("stub.lib"));
      }
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
  si_da_push(mem, files.objects, spn_path(mem, SPN_DIR_ID_RUNTIME, "zig/stub.c"));

  spn_invocation_t invocation = sp_zero;
  spn_gnu_render_link(mem, cc, profile, &warm->link, &files, &invocation);
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
        .toolchain = cc->name.str,
        .triple = warm->triple,
        .stub = warm->name,
        .run = spn_event_run(g->roots, &invocation, &run),
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
