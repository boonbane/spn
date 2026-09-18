#include "spn/errors.h"
#include "unit/types.h"

#include "dag/dag.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "session/invocation.h"

spn_err_t on_render_compdb(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_pkg_unit_t* unit = (spn_pkg_unit_t*)user_data;

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = spn_pkg_unit_write_compile_commands(g->roots, unit, spn_path_str(g->roots, s.mem, outputs[0]));
  sp_mem_end_scratch(s);
  return err ? SPN_ERR_DAG_OUTPUT_WRITE : SPN_OK;
}

spn_err_t spn_dag_exec_compile_commands_merge(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_da(sp_str_t) fragments = sp_da_new(s.mem, sp_str_t);
  sp_da_for(action->consumes, it) {
    sp_da_push(fragments, spn_path_str(g->roots, s.mem, spn_dag_find_artifact(g, action->consumes[it])->materialized));
  }
  spn_err_t err = spn_compile_commands_merge(fragments, spn_path_str(g->roots, s.mem, outputs[0]));
  if (!err) {
    sp_fs_create_file(spn_path_str(g->roots, s.mem, outputs[1]));
  }
  sp_mem_end_scratch(s);
  return err ? SPN_ERR_DAG_OUTPUT_WRITE : SPN_OK;
}
