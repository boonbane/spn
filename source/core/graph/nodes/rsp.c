#include "spn/errors.h"

#include "compiler/rsp.h"
#include "dag/node.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"

static spn_err_t write_rsp(sp_mem_t mem, const si_dag_ctx_t* c, si_rsp_t* rsp) {
  sp_io_file_writer_t writer = sp_zero;
  if (sp_io_file_writer_from_path_at(&writer, spn_path_at(c->roots, si_dag_resolve_output(c, rsp->output)))) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  sp_da(spn_arg_t) args = sp_da_new(mem, spn_arg_t);
  si_da_for(rsp->objects, it) {
    sp_da_push(args, spn_arg_path(si_dag_resolve_input(c, rsp->objects[it])));
  }
  spn_rsp_render(&writer.base, c->roots, rsp->style, args);
  sp_io_file_writer_close(&writer);
  return SPN_OK;
}

spn_err_t si_on_write_rsp(const si_dag_ctx_t* c, void* user_data) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = write_rsp(s.mem, c, (si_rsp_t*)user_data);
  sp_mem_end_scratch(s);
  return err;
}
