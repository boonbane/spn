#include "spn/errors.h"

#include "codegen/codegen.h"
#include "compiler/driver.h"
#include "dag/dag.h"
#include "graph/build.h"
#include "graph/dag.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "session/session.h"

static spn_err_t write_compile_commands(spn_dag_t* g, spn_dag_build_t* b, spn_path_t path) {
  const spn_path_roots_t* roots = g->roots;
  spn_session_t* session = b->session;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_mem_t mem = scratch.mem;

  sp_io_dyn_mem_writer_t buf;
  sp_io_dyn_mem_writer_init(mem, &buf);
  sp_io_writer_t* io = &buf.base;

  sp_io_write_cstr(io, "[", SP_NULLPTR);
  sp_om_for(session->units.objects, it) {
    spn_compile_unit_t* unit = sp_om_at(session->units.objects, it);
    spn_build_unit_t* build = unit->target->pkg->build;
    spn_profile_info_t profile = spn_dag_build_profile(g, *sp_ht_getp(b->ids.builds, build));
    spn_cc_compile_files_t files = {
      .source = unit->paths.file,
      .output = unit->paths.object,
    };
    spn_invocation_t invocation = spn_cc_render_compile_command(mem, &build->toolchain->cc, &profile, spn_session_get_object_plan(session, unit->id), &files);
    sp_da(sp_str_t) args = spn_invocation_args(roots, mem, &invocation);

    if (it) {
      sp_io_write_c8(io, ',');
    }
    sp_io_write_cstr(io, "\n  { \"directory\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, mem, invocation.cwd));
    sp_io_write_cstr(io, ", \"file\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, mem, files.source));
    sp_io_write_cstr(io, ", \"output\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, mem, files.output));
    sp_io_write_cstr(io, ", \"arguments\": [", SP_NULLPTR);
    spn_codegen_json_str(io, spn_arg_str(roots, mem, invocation.program));
    sp_da_for(args, arg) {
      sp_io_write_cstr(io, ", ", SP_NULLPTR);
      spn_codegen_json_str(io, args[arg]);
    }
    sp_io_write_cstr(io, "] }", SP_NULLPTR);
  }
  sp_io_write_cstr(io, "\n]\n", SP_NULLPTR);

  spn_err_t err = sp_fs_create_file_str_at(spn_path_at(roots, path), sp_io_dyn_mem_writer_as_str(&buf)) ? SPN_ERROR : SPN_OK;
  sp_mem_end_scratch(scratch);
  return err;
}

spn_err_t spn_dag_exec_compile_commands(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_err_t err = write_compile_commands(g, (spn_dag_build_t*)user_data, outputs[0]);
  if (!err) {
    sp_fs_create_file_at(spn_path_at(g->roots, outputs[1]));
  }
  return err ? SPN_ERR_DAG_OUTPUT_WRITE : SPN_OK;
}
