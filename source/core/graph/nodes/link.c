#include "core/types.h"
#include "ctx/types.h"
#include "spn/errors.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "event/event.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "unit/package.h"

spn_err_t si_on_link(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  si_link_t* ctx = (si_link_t*)user_data;
  spn_target_unit_t* target = ctx->target;
  spn_build_unit_t* build = target->pkg->build;
  spn_cc_t* cc = &build->toolchain->cc;
  spn_cc_link_files_t files = {
    .output = outputs[0],
    .implib = spn_path_empty(ctx->link->implib) ? sp_zero_s(spn_path_t) : outputs[1],
    .libc = si_dag_build_libc(g, ctx->build),
  };

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_da(spn_arg_t) objects = si_dag_build_objects(scratch.mem, g, &ctx->objects);
  spn_invocation_t invocation = sp_zero;
  switch (cc->driver) {
    case SPN_CC_DRIVER_GCC:
    case SPN_CC_DRIVER_CLANG:
    case SPN_CC_DRIVER_ZIG: {
      spn_gnu_render_link(scratch.mem, cc, &build->profile, ctx->link, objects, &files, &invocation);
      break;
    }
    case SPN_CC_DRIVER_MSVC: {
      spn_msvc_render_link(scratch.mem, cc, &build->profile, ctx->link, objects, &files, &invocation);
      break;
    }
    case SPN_CC_DRIVER_NONE: {
      sp_unreachable_case();
    }
  }
  invocation.cwd = target->pkg->paths.work;

  spn_pkg_unit_announce_compile(target->pkg);
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_START,
    .pkg = target->pkg->info->name.str,
    .link_start = {
      .target = target->info->name.str,
    }
  });

  spn_invocation_result_t run = spn_invocation_run(g->roots, &invocation);
  sp_str_t command = spn_invocation_to_str(g->roots, spn.events->mem, &invocation);
  sp_mem_end_scratch(scratch);

  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_LINK_FAILED,
      .pkg = target->pkg->info->name.str,
      .link_failed = {
        .target = target->info->name.str,
        .exit_code = run.result.status.exit_code,
        .command = command,
        .out = run.result.out,
        .err = run.result.err,
      }
    });
    return SPN_ERR_DAG_ACTION;
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_LINK_PASSED,
    .pkg = target->pkg->info->name.str,
    .link_passed = {
      .target = target->info->name.str,
      .output_path = target->paths.output,
      .command = command,
      .out = run.result.out,
      .time = run.elapsed,
    }
  });
  return SPN_OK;
}
