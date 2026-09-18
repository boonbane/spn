#include "spn/errors.h"

#include "compiler/rsp.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"

spn_err_t on_render_rsp(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_dag_rsp_ctx_t* rsp = (spn_dag_rsp_ctx_t*)user_data;

  sp_io_file_writer_t writer = sp_zero;
  if (spn_path_open_writer(g->roots, outputs[0], &writer)) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  spn_rsp_render(&writer.base, g->roots, rsp->style, rsp->args);
  sp_io_file_writer_close(&writer);
  return SPN_OK;
}
