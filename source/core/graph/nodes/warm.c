#include "ctx/types.h"
#include "core/core.h"
#include "event/event.h"

#include "compiler/driver.h"
#include "compiler/rsp.h"
#include "dag/node.h"
#include "external/zig.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "triple/triple.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"

typedef struct {
  sp_io_writer_t base;
  spn_zig_progress_t progress;
  const si_dag_ctx_t* c;
  u64 last;
} progress_writer_t;

static sp_err_t progress_write(sp_io_writer_t* writer, const void* bytes, u64 len, u64* written) {
  progress_writer_t* w = (progress_writer_t*)writer;
  *written = len;
  if (!spn_zig_progress_feed(&w->progress, bytes, len)) {
    return SP_OK;
  }
  u64 ticks = spn_zig_progress_ticks(&w->progress);
  si_dag_add_warm(w->c, (s64)(ticks - w->last));
  w->last = ticks;
  return SP_OK;
}

static spn_err_t zig_warmup(sp_mem_t mem, const si_dag_ctx_t* c, si_zig_warmup_t* warm) {
  const spn_cc_t* cc = warm->cc;
  const spn_profile_info_t* profile = warm->profile;
  const spn_path_roots_t* roots = c->roots;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_WARM_START,
    .warm = {
      .toolchain = cc->name.str,
      .triple = warm->triple,
      .stub = warm->name,
    },
  });

  spn_path_t staging = c->scratch;
  spn_cc_link_files_t files = {
    .cwd = staging,
    .output = spn_path_join(mem, staging, sp_str_lit("stub.bin")),
  };
  if (warm->libc.occupied) {
    files.libc = si_dag_resolve_input(c, warm->libc);
  }
  switch (warm->link.kind) {
    case SPN_CC_OUTPUT_SHARED_LIB:
    case SPN_CC_OUTPUT_REACTOR: {
      spn_cc_exports_format_t format = spn_cc_exports_format(warm->link.kind, spn_os_to_native_object_format(profile->os));
      sp_str_t extension = spn_cc_exports_extension(format);
      files.exports = spn_path_join(mem, staging, sp_fmt(mem, "stub.{}", sp_fmt_str(extension)).value);
      sp_io_file_writer_t writer = sp_zero;
      if (sp_io_file_writer_from_path_at(&writer, spn_path_at(roots, files.exports))) {
        return SPN_ERR_DAG_OUTPUT_WRITE;
      }
      spn_cc_render_exports(&writer.base, roots, format, spn_rsp_style(cc->driver), sp_str_lit("stub"), SP_NULLPTR);
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

  spn_cc_link_plan_t plan = spn_gnu_render_link(mem, cc, profile, &warm->link);
  spn_invocation_t invocation = spn_cc_render_link_command(mem, &plan, &files);

  progress_writer_t progress = { .base = { .write = progress_write }, .c = c };
  spn_invocation_result_t run = spn_invocation_run_progress(roots, &invocation, spn_path_join(mem, staging, sp_str_lit("log")), &progress.base);
  if (progress.last) {
    si_dag_add_warm(c, -(s64)progress.last);
  }

  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_WARM_FAILED,
      .warm_failed = {
        .toolchain = cc->name.str,
        .triple = warm->triple,
        .stub = warm->name,
        .run = spn_event_run(roots, &invocation, &run),
      },
    });
    return SPN_ERR_DAG_ACTION;
  }
  if (sp_fs_create_file_at(spn_path_at(roots, si_dag_resolve_output(c, warm->stamp)))) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  return SPN_OK;
}

spn_err_t si_on_zig_warmup(const si_dag_ctx_t* c, void* user_data) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = zig_warmup(s.mem, c, (si_zig_warmup_t*)user_data);
  sp_mem_end_scratch(s);
  return err;
}
