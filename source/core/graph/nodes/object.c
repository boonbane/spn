#include "ctx/types.h"
#include "event/event.h"
#include "session/types.h"
#include "macro/macro.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "dag/wasi/canonicalize.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "session/session.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "unit/package.h"

static s32 run_compiler(spn_compile_unit_t* unit, spn_path_t object, spn_path_t depfile) {
  spn_pkg_unit_t* pkg = unit->target->pkg;
  spn_session_t* session = pkg->session;

  spn_pkg_unit_announce_compile(pkg);

  spn_cc_compile_files_t files = {
    .source = unit->paths.file,
    .output = object,
    .depfile = depfile,
  };
  spn_invocation_t invocation = spn_cc_render_compile_command(spn.mem, &pkg->build->toolchain->cc, &pkg->build->profile, &unit->invocation, &files);
  sp_str_t source = spn_path_str(&spn.roots, spn.mem, files.source);
  sp_str_t output = spn_path_str(&spn.roots, spn.mem, files.output);
  spn_invocation_result_t run = spn_invocation_run(&invocation);
  sp_str_t command = spn_invocation_to_str(spn.mem, &invocation);

  if (run.result.status.exit_code) {
    spn_event_buffer_push(session->ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_TARGET_BUILD_FAILED,
      .pkg = pkg->info->name,
      .target_failed = {
        .target = unit->target->info->name,
        .source_file = source,
        .object_file = output,
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
        .source_file = source,
        .object_file = output,
        .command = command,
        .out = run.result.out,
        .time = run.elapsed,
      }
    });
  }

  return run.result.status.exit_code;
}

static spn_err_t compile_object(sp_mem_t scratch, spn_dag_t* g, spn_compile_unit_t* unit, spn_dag_env_t* env, spn_path_t object, spn_dag_obs_set_t* obs) {
  const spn_cc_toolchain_t* toolchain = &unit->target->pkg->build->toolchain->cc;

  spn_cc_depfile_t mode = spn_cc_depfile(toolchain, unit->lang);
  if (mode == SPN_CC_DEPFILE_NONE) {
    return run_compiler(unit, object, (spn_path_t) sp_zero) ? SPN_ERR_DAG_ACTION : SPN_OK;
  }

  spn_path_t depfile = spn_path_suffix(scratch, object, sp_str_lit(".d"));
  if (run_compiler(unit, object, depfile)) {
    return SPN_ERR_DAG_ACTION;
  }

  sp_str_t dep = spn_path_str(g->roots, scratch, depfile);
  if (!sp_fs_exists(dep)) {
    return mode == SPN_CC_DEPFILE_REQUIRED ? SPN_ERR_DAG_DEPFILE : SPN_OK;
  }
  sp_str_t content = sp_zero;
  sp_da(sp_str_t) prereqs = sp_zero;
  if (sp_io_read_file(scratch, dep, &content) || spn_cc_parse_depfile(scratch, toolchain, content, &prereqs)) {
    return SPN_ERR_DAG_DEPFILE;
  }
  sp_da_for(prereqs, it) {
    sp_str_t path = prereqs[it];
    if (!sp_fs_is_absolute(path)) {
      path = spn_path_str(g->roots, scratch, spn_path_join(scratch, unit->target->pkg->paths.work, path));
    }
    sp_str_t canonical = spn_dag_file_cache_canonical(env->files, path);
    if (sp_str_empty(canonical)) {
      canonical = spn_dag_wasi_canonicalize(scratch, path);
    }
    sp_assert(!sp_str_empty(canonical));
    spn_dag_observe(obs, (spn_dag_obs_t) {
      .kind = SPN_DAG_OBS_FILE,
      .path = spn_path_make(g->roots, canonical),
    });
  }
  return SPN_OK;
}

spn_err_t on_compile_object(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = compile_object(s.mem, g, (spn_compile_unit_t*)user_data, env, outputs[0], obs);
  sp_mem_end_scratch(s);
  return err;
}
