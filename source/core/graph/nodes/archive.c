#include "core/types.h"
#include "ctx/types.h"
#include "spn/errors.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "event/event.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "session/invocation.h"

spn_err_t si_on_archive(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  si_archive_t* ctx = (si_archive_t*)user_data;

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_cc_archive_files_t files = { .output = outputs[0] };
  switch (ctx->objects.kind) {
    case SPN_DAG_BUILD_OBJECTS_DIRECT: {
      files.objects = si_dag_build_paths(scratch.mem, g, ctx->objects.ids);
      break;
    }
    case SPN_DAG_BUILD_OBJECTS_RSP: {
      files.rsp = si_dag_get_artifact_path(g, ctx->objects.rsp);
      break;
    }
  }
  spn_invocation_t invocation = spn_cc_render_archive_command(scratch.mem, ctx->plan, &files);

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_START,
    .link_start = { .subject = ctx->subject },
  });

  spn_invocation_result_t run = spn_invocation_run(g->roots, &invocation);
  spn_event_run_t report = spn_event_run(g->roots, &invocation, &run);
  sp_mem_end_scratch(scratch);

  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_LINK_FAILED,
      .link_failed = { .subject = ctx->subject, .run = report },
    });
    return SPN_ERR_DAG_ACTION;
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_PASSED,
    .link_passed = { .subject = ctx->subject, .output_path = ctx->output_path, .run = report },
  });
  return SPN_OK;
}
