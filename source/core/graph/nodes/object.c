#include "ctx/types.h"
#include "event/event.h"
#include "session/types.h"
#include "macro/macro.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "session/session.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "unit/package.h"

static s32 run_compiler(const spn_path_roots_t* roots, spn_compile_unit_t* unit, const spn_invocation_t* base, const spn_profile_info_t* profile, spn_path_t object, spn_path_t depfile) {
  spn_pkg_unit_t* pkg = unit->target->pkg;
  spn_session_t* session = pkg->session;

  spn_pkg_unit_announce_compile(pkg);

  spn_cc_compile_files_t files = {
    .source = unit->paths.file,
    .output = object,
    .depfile = depfile,
  };
  spn_invocation_t invocation = spn_cc_render_compile_command(spn.mem, &pkg->build->toolchain->cc, profile, base, &files);
  spn_invocation_result_t run = spn_invocation_run(roots, &invocation);
  sp_str_t command = spn_invocation_to_str(roots, spn.mem, &invocation);

  if (run.result.status.exit_code) {
    spn_event_buffer_push(session->ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_TARGET_BUILD_FAILED,
      .pkg = pkg->info->name,
      .target_failed = {
        .target = unit->target->info->name,
        .source_file = unit->paths.file,
        .object_file = unit->paths.object,
        .rc = run.result.status.exit_code,
        .out = run.result.out,
        .command = command,
        .time = run.elapsed,
      }
    });
  } else {
    spn_event_buffer_push(session->ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_TARGET_BUILD_PASSED,
      .pkg = pkg->info->name,
      .target_passed = {
        .target = unit->target->info->name,
        .source_file = unit->paths.file,
        .object_file = unit->paths.object,
        .command = command,
        .out = run.result.out,
        .time = run.elapsed,
      }
    });
  }

  return run.result.status.exit_code;
}

static spn_err_t compile_object(sp_mem_t scratch, spn_dag_t* g, spn_dag_object_ctx_t* ctx, spn_dag_env_t* env, spn_path_t object, spn_dag_obs_set_t* obs) {
  spn_compile_unit_t* unit = ctx->unit;
  const spn_cc_t* toolchain = &unit->target->pkg->build->toolchain->cc;
  spn_profile_info_t profile = spn_dag_build_profile(g, ctx->build);

  spn_cc_depfile_t mode = spn_cc_depfile(toolchain, unit->lang);
  if (mode == SPN_CC_DEPFILE_NONE) {
    return run_compiler(g->roots, unit, ctx->invocation, &profile, object, (spn_path_t) sp_zero) ? SPN_ERR_DAG_ACTION : SPN_OK;
  }

  spn_path_t depfile = spn_path_concat(scratch, object, ".d");
  if (run_compiler(g->roots, unit, ctx->invocation, &profile, object, depfile)) {
    return SPN_ERR_DAG_ACTION;
  }

  sp_path_t dep = spn_path_at(g->roots, depfile);
  if (!sp_fs_exists_at(dep)) {
    return mode == SPN_CC_DEPFILE_REQUIRED ? SPN_ERR_DAG_DEPFILE : SPN_OK;
  }
  sp_str_t content = sp_zero;
  sp_da(sp_str_t) prereqs = sp_zero;
  if (sp_io_read_file_at(scratch, dep, &content) || spn_cc_parse_depfile(scratch, toolchain, content, &prereqs)) {
    return SPN_ERR_DAG_DEPFILE;
  }
  sp_da_for(prereqs, it) {
    spn_path_t path = spn_path_resolve(scratch, unit->target->pkg->paths.work, prereqs[it]);
    spn_dag_observe(obs, (spn_dag_obs_t) {
      .kind = SPN_DAG_OBS_FILE,
      .path = spn_dag_file_cache_canonical(env->files, path),
    });
  }
  return SPN_OK;
}

spn_err_t on_compile_object(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_dag_object_ctx_t* ctx = sp_ptr_cast(spn_dag_object_ctx_t*, user_data);
  spn_path_t output = outputs[0];

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = compile_object(s.mem, g, ctx, env, output, obs);
  sp_mem_end_scratch(s);
  return err;
}
