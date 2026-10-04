#include "spn/errors.h"

#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "toolchain/libc.h"

spn_err_t spn_dag_exec_libc(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  const spn_sdk_t* sdk = (const spn_sdk_t*)user_data;

  sp_io_file_writer_t writer = sp_zero;
  if (sp_io_file_writer_from_path_at(&writer, spn_path_at(g->roots, outputs[0]))) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  spn_libc_render(&writer.base, g->roots, sdk);
  sp_io_file_writer_close(&writer);
  return SPN_OK;
}
