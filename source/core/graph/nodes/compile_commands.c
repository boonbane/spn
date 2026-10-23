#include "spn/errors.h"

#include "codegen/codegen.h"
#include "compiler/driver.h"
#include "dag/node.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "session/invocation.h"
#include "session/types.h"

spn_err_t si_on_write_compile_commands(const si_dag_ctx_t* c, void* user_data) {
  si_compile_commands_t* compile_commands = (si_compile_commands_t*)user_data;
  spn_session_t* session = compile_commands->session;
  const spn_path_roots_t* roots = c->roots;

  sp_io_dyn_mem_writer_t buf;
  sp_io_dyn_mem_writer_init(c->mem, &buf);
  sp_io_writer_t* io = &buf.base;

  sp_io_write_cstr(io, "[", SP_NULLPTR);
  sp_om_for(session->units.objects, it) {
    spn_compile_unit_t* unit = sp_om_at(session->units.objects, it);
    spn_invocation_t invocation = spn_cc_render_compile_source(c->mem, &unit->plan.base, unit->file);
    sp_da(sp_str_t) args = spn_invocation_args(roots, c->mem, &invocation);

    if (it) {
      sp_io_write_c8(io, ',');
    }
    sp_io_write_cstr(io, "\n  { \"directory\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, c->mem, session->paths.root));
    sp_io_write_cstr(io, ", \"file\": ", SP_NULLPTR);
    spn_codegen_json_str(io, spn_path_str(roots, c->mem, unit->file));
    sp_io_write_cstr(io, ", \"arguments\": [", SP_NULLPTR);
    spn_codegen_json_str(io, spn_arg_str(roots, c->mem, invocation.program));
    sp_da_for(args, arg) {
      sp_io_write_cstr(io, ", ", SP_NULLPTR);
      spn_codegen_json_str(io, args[arg]);
    }
    sp_io_write_cstr(io, "] }", SP_NULLPTR);
  }
  sp_io_write_cstr(io, "\n]\n", SP_NULLPTR);

  if (sp_fs_create_file_str_at(spn_path_at(roots, si_dag_resolve_output(c, compile_commands->file)), sp_io_dyn_mem_writer_as_str(&buf))) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  sp_fs_create_file_at(spn_path_at(roots, si_dag_resolve_output(c, compile_commands->stamp)));
  return SPN_OK;
}
