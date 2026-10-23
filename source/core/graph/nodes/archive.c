#include "core/types.h"
#include "ctx/types.h"
#include "spn/errors.h"

#include "compiler/driver.h"
#include "dag/node.h"
#include "event/event.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "session/invocation.h"

spn_err_t si_on_archive(const si_dag_ctx_t* c, void* user_data) {
  si_archive_t* archive = (si_archive_t*)user_data;

  spn_cc_archive_files_t files = { .cwd = c->scratch, .output = si_dag_resolve_output(c, archive->output) };
  switch (archive->objects.kind) {
    case SPN_DAG_BUILD_OBJECTS_DIRECT: {
      files.objects = si_dag_build_paths(c, archive->objects.ids);
      break;
    }
    case SPN_DAG_BUILD_OBJECTS_RSP: {
      files.rsp = si_dag_resolve_input(c, archive->objects.rsp);
      break;
    }
  }
  spn_invocation_t invocation = spn_cc_render_archive_command(c->mem, archive->plan, &files);

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
    .link_passed = { .subject = archive->subject, .output_path = archive->output_path, .run = report },
  });
  return SPN_OK;
}
