#include "compiler/types.h"
#include "ctx/types.h"
#include "event/event.h"
#include "spn/core.h"
#include "spn/err.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"

static spn_err_t run_compiler(const spn_path_roots_t* roots, si_compile_t* ctx, const spn_cc_compile_files_t* files) {
  spn_invocation_t invocation = spn_cc_render_compile_command(spn.mem, ctx->cc, ctx->profile, ctx->base, files);
  spn_invocation_result_t run = spn_invocation_run(roots, &invocation);
  spn_event_run_t report = spn_event_run(roots, &invocation, &run);

  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_TARGET_BUILD_FAILED,
      .target_failed = { .subject = ctx->subject, .source_file = ctx->source, .run = report },
    });
  } else {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_TARGET_BUILD_PASSED,
      .target_passed = { .subject = ctx->subject, .source_file = ctx->source, .run = report },
    });
  }

  return run.result.status.exit_code ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static spn_err_t compile_object(sp_mem_t scratch, spn_dag_t* g, si_compile_t* ctx, spn_dag_env_t* env, spn_path_t object, spn_dag_obs_set_t* obs) {
  const spn_cc_t* toolchain = ctx->cc;
  spn_cc_compile_files_t files = {
    .source = ctx->source,
    .output = object,
  };
  if (ctx->libc.occupied) {
    files.libc = si_dag_get_artifact_path(g, ctx->libc);
  }

  if (ctx->depfile != SPN_CC_DEPFILE_NONE) {
    files.depfile = spn_path_concat(scratch, object, ".d");
  }

  spn_try(run_compiler(g->roots, ctx, &files));
  if (ctx->depfile == SPN_CC_DEPFILE_NONE) return SPN_OK;

  sp_path_t dep = spn_path_at(g->roots, files.depfile);
  if (!sp_fs_exists_at(dep)) {
    return ctx->depfile == SPN_CC_DEPFILE_REQUIRED ? SPN_ERR_DAG_DEPFILE : SPN_OK;
  }

  sp_str_t content = sp_zero;
  sp_da(sp_str_t) prereqs = sp_zero;
  if (sp_io_read_file_at(scratch, dep, &content) || spn_cc_parse_depfile(scratch, toolchain, content, &prereqs)) {
    return SPN_ERR_DAG_DEPFILE;
  }
  sp_da_for(prereqs, it) {
    spn_path_t path = spn_path_resolve(scratch, ctx->base->cwd, prereqs[it]);
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
