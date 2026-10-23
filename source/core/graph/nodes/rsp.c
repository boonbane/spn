#include "spn/errors.h"

#include "compiler/rsp.h"
#include "dag/dag.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"

spn_err_t si_on_write_rsp(const si_dag_ctx_t* c, void* user_data) {
  si_rsp_t* rsp = (si_rsp_t*)user_data;

  sp_io_file_writer_t writer = sp_zero;
  if (sp_io_file_writer_from_path_at(&writer, spn_path_at(c->roots, si_dag_resolve_output(c, rsp->output)))) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  sp_da(spn_arg_t) args = sp_da_new(c->mem, spn_arg_t);
  si_da_for(rsp->objects, it) {
    sp_da_push(args, spn_arg_path(si_dag_resolve_input(c, rsp->objects[it])));
  }
  spn_rsp_render(&writer.base, c->roots, rsp->style, args);
  sp_io_file_writer_close(&writer);
  return SPN_OK;
}
