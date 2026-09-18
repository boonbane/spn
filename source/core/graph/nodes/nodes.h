#ifndef SPN_GRAPH_NODES_H
#define SPN_GRAPH_NODES_H

#include "compiler/types.h"
#include "dag/types.h"
#include "sp.h"
#include "spn/core.h"
#include "external/cc.h"
#include "core/types.h"
#include "unit/types.h"

typedef struct {
  spn_target_unit_t* target;
  spn_cc_archive_files_t files;
} spn_dag_archive_ctx_t;

typedef struct {
  spn_target_unit_t* target;
  spn_cc_link_files_t files;
} spn_dag_link_ctx_t;

typedef struct {
  spn_rsp_style_t style;
  sp_da(spn_arg_t) args;
} spn_dag_rsp_ctx_t;

spn_err_t on_compile_object(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t on_archive_target(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t on_link_target(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t on_render_exports(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t on_render_rsp(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t on_build_embedding(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t spn_dag_exec_user(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t on_publish_tree(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t on_render_compdb(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t spn_dag_exec_compile_commands_merge(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);

#endif
