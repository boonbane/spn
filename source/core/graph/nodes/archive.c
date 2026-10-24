#include "core/types.h"
#include "ctx/types.h"
#include "spn/errors.h"

#include "compiler/driver.h"
#include "dag/node.h"
#include "event/event.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "session/invocation.h"

static spn_err_t run_archiver(sp_mem_t mem, const si_dag_ctx_t* c, si_archive_t* archive) {
  spn_cc_archive_files_t files = { .cwd = c->scratch, .output = si_dag_resolve_output(c, archive->output) };
  switch (archive->objects.kind) {
    case SPN_DAG_BUILD_OBJECTS_DIRECT: {
      files.objects = si_dag_build_paths(mem, c, archive->objects.ids);
      break;
    }
    case SPN_DAG_BUILD_OBJECTS_RSP: {
      files.rsp = si_dag_resolve_input(c, archive->objects.rsp);
      break;
    }
  }
  spn_invocation_t invocation = spn_cc_render_archive_command(mem, archive->plan, &files);

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_START,
    .link_start = { .subject = archive->subject },
  });

  spn_invocation_result_t run = spn_invocation_run(c->roots, &invocation);
  spn_event_run_t report = spn_event_run(c->roots, &invocation, &run);

  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_LINK_FAILED,
      .link_failed = { .subject = archive->subject, .run = report },
    });
    return SPN_ERR_DAG_ACTION;
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_PASSED,
    .link_passed = { .subject = archive->subject, .file_name = archive->file_name, .run = report },
  });
  return SPN_OK;
}

spn_err_t si_on_archive(const si_dag_ctx_t* c, void* user_data) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = run_archiver(s.mem, c, (si_archive_t*)user_data);
  sp_mem_end_scratch(s);
  return err;
}
