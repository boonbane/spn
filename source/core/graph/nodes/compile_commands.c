#include "spn/errors.h"

#include "dag/dag.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "session/invocation.h"

spn_err_t spn_dag_exec_compile_commands(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_err_t err = spn_session_write_compile_commands(g->roots, (spn_session_t*)user_data, outputs[0]);
  if (!err) {
    sp_fs_create_file_at(spn_path_at(g->roots, outputs[1]));
  }
  return err ? SPN_ERR_DAG_OUTPUT_WRITE : SPN_OK;
}
