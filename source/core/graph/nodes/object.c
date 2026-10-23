#include "compiler/types.h"
#include "ctx/types.h"
#include "event/event.h"
#include "spn/core.h"
#include "spn/err.h"

#include "compiler/driver.h"
#include "dag/node.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"

static spn_err_t run_compiler(const si_dag_ctx_t* c, si_compile_t* compile, const spn_cc_compile_files_t* files) {
  spn_invocation_t invocation = spn_cc_render_compile_command(c->mem, compile->plan, files);
  spn_invocation_result_t run = spn_invocation_run(c->roots, &invocation);
  spn_event_run_t report = spn_event_run(c->roots, &invocation, &run);

  if (run.result.status.exit_code) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_TARGET_BUILD_FAILED,
      .target_failed = { .subject = compile->subject, .source_file = compile->source, .run = report },
    });
  } else {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_TARGET_BUILD_PASSED,
      .target_passed = { .subject = compile->subject, .source_file = compile->source, .run = report },
    });
  }

  return run.result.status.exit_code ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static spn_cc_compile_files_t compile_files(const si_dag_ctx_t* c, const si_compile_t* compile) {
  spn_cc_compile_files_t files = {
    .cwd = c->scratch,
    .source = compile->source,
    .output = si_dag_resolve_output(c, compile->object),
  };
  if (compile->libc.occupied) {
    files.libc = si_dag_resolve_input(c, compile->libc);
  }
  return files;
}

spn_err_t si_on_compile(const si_dag_ctx_t* c, void* user_data) {
  si_compile_t* compile = sp_ptr_cast(si_compile_t*, user_data);
  spn_cc_compile_files_t files = compile_files(c, compile);
  return run_compiler(c, compile, &files);
}

spn_err_t si_on_compile_discover(const si_dag_ctx_t* c, void* user_data) {
  si_compile_t* compile = sp_ptr_cast(si_compile_t*, user_data);
  spn_cc_compile_files_t files = compile_files(c, compile);
  files.depfile = spn_path_join(c->mem, c->scratch, sp_str_lit("depfile"));

  spn_try(run_compiler(c, compile, &files));

  sp_path_t dep = spn_path_at(c->roots, files.depfile);
  if (!sp_fs_exists_at(dep)) {
    return compile->plan->depfile == SPN_CC_DEPFILE_REQUIRED ? SPN_ERR_DAG_DEPFILE : SPN_OK;
  }

  sp_str_t content = sp_zero;
  sp_da(sp_str_t) prereqs = sp_zero;
  if (sp_io_read_file_at(c->mem, dep, &content) || spn_cc_parse_depfile(c->mem, compile->plan->driver, content, &prereqs)) {
    return SPN_ERR_DAG_DEPFILE;
  }
  sp_da_for(prereqs, it) {
    spn_path_t path = spn_path_resolve(c->mem, files.cwd, prereqs[it]);
    si_dag_observe(c, (spn_dag_obs_t) {
      .kind = SPN_DAG_OBS_FILE,
      .path = si_dag_canonicalize_path(c, path),
    });
  }
  return SPN_OK;
}
