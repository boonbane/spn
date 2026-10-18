#include "compiler/types.h"
#include "ctx/types.h"
#include "event/event.h"
#include "spn/core.h"
#include "spn/err.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "unit/package.h"

static spn_err_t run_compiler(const spn_path_roots_t* roots, spn_compile_unit_t* unit, const spn_invocation_t* base, const spn_cc_compile_files_t* files) {
  spn_pkg_unit_t* pkg = unit->target->pkg;
  spn_session_t* session = pkg->session;

  spn_pkg_unit_announce_compile(pkg);

  spn_invocation_t invocation = spn_cc_render_compile_command(spn.mem, &pkg->build->toolchain->cc, &pkg->build->profile, base, files);
  spn_invocation_result_t run = spn_invocation_run(roots, &invocation);
  sp_str_t command = spn_invocation_to_str(roots, spn.events->mem, &invocation);

  if (run.result.status.exit_code) {
    spn_event_buffer_push(session->ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_TARGET_BUILD_FAILED,
      .pkg = pkg->info->name.str,
      .target_failed = {
        .target = unit->target->info->name.str,
        .source_file = unit->file,
        .rc = run.result.status.exit_code,
        .out = run.result.out,
        .command = command,
        .time = run.elapsed,
      }
    });
  } else {
    spn_event_buffer_push(session->ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_TARGET_BUILD_PASSED,
      .pkg = pkg->info->name.str,
      .target_passed = {
        .target = unit->target->info->name.str,
        .source_file = unit->file,
        .command = command,
        .out = run.result.out,
        .time = run.elapsed,
      }
    });
  }

  return run.result.status.exit_code ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static spn_err_t compile_object(sp_mem_t scratch, spn_dag_t* g, si_compile_t* ctx, spn_dag_env_t* env, spn_path_t object, spn_dag_obs_set_t* obs) {
  spn_compile_unit_t* unit = ctx->unit;
  const spn_cc_t* toolchain = &unit->target->pkg->build->toolchain->cc;
  spn_cc_compile_files_t files = {
    .source = unit->file,
    .output = object,
    .libc = si_dag_build_libc(g, ctx->build),
  };

  struct { bool wanted; bool required; } depfile = { true, true };
  if (unit->lang == SPN_LANG_ASM) {
    if (toolchain->driver == SPN_CC_DRIVER_MSVC) {
      depfile.wanted = false;
    } else {
      depfile.required = false;
    }
  }

  if (depfile.wanted) {
    files.depfile = spn_path_concat(scratch, object, ".d");
  }

  spn_try(run_compiler(g->roots, unit, &unit->base, &files));
  if (!depfile.wanted) return SPN_OK;

  sp_path_t dep = spn_path_at(g->roots, files.depfile);
  if (!sp_fs_exists_at(dep)) {
    if (!depfile.required) return SPN_OK;
    return SPN_ERR_DAG_DEPFILE;
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

spn_err_t si_on_compile(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  si_compile_t* ctx = sp_ptr_cast(si_compile_t*, user_data);
  spn_path_t output = outputs[0];

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = compile_object(s.mem, g, ctx, env, output, obs);
  sp_mem_end_scratch(s);
  return err;
}
