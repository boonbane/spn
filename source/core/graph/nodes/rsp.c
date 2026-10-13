#include "spn/errors.h"

#include "compiler/rsp.h"
#include "dag/dag.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"

spn_err_t si_on_write_rsp(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  si_rsp_t* rsp = (si_rsp_t*)user_data;

  sp_io_file_writer_t writer = sp_zero;
  if (sp_io_file_writer_from_path_at(&writer, spn_path_at(g->roots, outputs[0]))) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_rsp_render(&writer.base, g->roots, rsp->style, spn_dag_build_object_args(s.mem, g, rsp->objects));
  sp_mem_end_scratch(s);
  sp_io_file_writer_close(&writer);
  return SPN_OK;
}
