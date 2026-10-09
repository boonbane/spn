#ifndef SPN_GRAPH_NODES_H
#define SPN_GRAPH_NODES_H

#include "compiler/types.h"
#include "dag/types.h"
#include "sp.h"
#include "spn/core.h"
#include "external/cc.h"
#include "core/types.h"
#include "graph/build.h"
#include "unit/types.h"

typedef struct {
  spn_target_unit_t* target;
  const spn_cc_link_t* link;
  sp_da(spn_arg_t) objects;
  const spn_dag_build_ctx_t* build;
} si_link_t;

typedef struct {
  spn_compile_unit_t* unit;
  const spn_invocation_t* invocation;
  const spn_dag_build_ctx_t* build;
} si_compile_t;

typedef struct {
  spn_rsp_style_t style;
  sp_da(spn_arg_t) args;
} si_rsp_t;

typedef struct {
  const spn_dag_build_ctx_t* build;
  spn_cc_link_t link;
  sp_str_t name;
  sp_str_t triple;
} si_zig_warmup_t;

spn_err_t si_on_compile(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_archive(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_link(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_write_exports(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_write_rsp(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_write_libc(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_write_compile_commands(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_embed(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_user_node(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_publish(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_zig_warmup(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);

#endif
