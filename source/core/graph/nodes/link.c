#include "core/types.h"
#include "ctx/types.h"
#include "spn/errors.h"

#include "compiler/driver.h"
#include "dag/node.h"
#include "event/event.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "session/invocation.h"

spn_err_t si_on_link(const si_dag_ctx_t* c, void* user_data) {
  si_link_t* link = (si_link_t*)user_data;

  spn_cc_link_files_t files = {
    .cwd = c->scratch,
    .output = si_dag_resolve_output(c, link->output),
    .scripts = si_dag_build_paths(c, link->scripts),
    .archives = si_dag_build_paths(c, link->deps.archives),
    .shared = si_dag_build_paths(c, link->deps.shared),
    .whole_archives = si_dag_build_paths(c, link->deps.whole_archives),
  };
  if (link->libc.occupied) {
    files.libc = si_dag_resolve_input(c, link->libc);
  }
  switch (link->objects.kind) {
    case SPN_DAG_BUILD_OBJECTS_DIRECT: {
      files.objects = si_dag_build_paths(c, link->objects.ids);
      break;
    }
    case SPN_DAG_BUILD_OBJECTS_RSP: {
      files.rsp = si_dag_resolve_input(c, link->objects.rsp);
      break;
    }
  }
  if (link->exports.occupied) {
    files.exports = si_dag_resolve_input(c, link->exports);
  }
  if (link->implib.occupied) {
    files.implib = si_dag_resolve_output(c, link->implib);
  }

  spn_invocation_t invocation = spn_cc_render_link_command(c->mem, link->plan, &files);

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_START,
    .link_start = { .subject = link->subject },
  });

  spn_invocation_result_t run = spn_invocation_run(c->roots, &invocation);
  spn_event_run_t report = spn_event_run(c->roots, &invocation, &run);

  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_LINK_FAILED,
      .link_failed = { .subject = link->subject, .run = report },
    });
    return SPN_ERR_DAG_ACTION;
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_PASSED,
    .link_passed = { .subject = link->subject, .output_path = link->output_path, .run = report },
  });
  return SPN_OK;
}
