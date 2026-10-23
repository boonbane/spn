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

// - I never liked the implicit interface of produces/outputs being arrays that you must index into properly. It's always better
// to, wherever you create the action and its outputs/whatever to name the thing and keep a ref to it rather than index the array
// Maybe we should just totally nuke access to the arrays and make it so you do that instead? Publish needs all of them, so maybe
// we give access to the arrays but make it convention to put specific things you need as IDs on the user data type?

// opaque pointer, holds the dag* and whatever else you need to do stuff from inside the callback, like
// iterating outputs. this probably just replaces the dag_attempt struct? cuz it just represents all the stuff
// for the attempt. by replaces, I mean that the opaque pointer is just cast to the attempt* internally
// in the DAG code and we add the fields we need?
typedef struct si_dag_whatever_t si_dag_whatever_t;

typedef struct {
  si_dag_whatever_t* handle;
  const spn_path_roots_t* roots;
  spn_path_t cwd; // Just the scratch dir
} si_action_ctx_t;

typedef struct {
  si_action_ctx_t* ctx;
  // ...whatever other iterator state
} si_action_file_it_t;

typedef struct {
  spn_path_t path;
} si_action_file_it_entry_t;

si_action_file_it_t si_action_consumed_files_it(si_action_ctx_t* ctx);
si_action_file_it_t si_action_produced_files_it(si_action_ctx_t* ctx);
bool si_action_file_it_next(si_action_file_it_t* it);

typedef struct {
  const spn_cc_link_plan_t* plan;
  spn_path_t output_path;
  spn_event_subject_t subject;
  spn_dag_build_objects_t objects;
  si_da(spn_dag_id_t) scripts;
  struct {
    si_da(spn_dag_id_t) archives;
    si_da(spn_dag_id_t) shared;
    si_da(spn_dag_id_t) whole_archives;
  } deps;
  spn_dag_id_t exports;
  spn_dag_id_t implib;
  spn_dag_id_t libc;
} si_link_t;

typedef struct {
  const spn_cc_archive_plan_t* plan;
  spn_path_t output_path;
  spn_event_subject_t subject;
  spn_dag_build_objects_t objects;
} si_archive_t;

typedef struct {
  const spn_cc_archive_plan_t* archive;
  spn_cc_exports_format_t format;
  spn_rsp_style_t rsp;
  sp_str_t library;
  spn_event_subject_t subject;
  spn_dag_build_objects_t objects;
  si_da(spn_dag_id_t) whole_archives;
} si_exports_t;

typedef struct {
  const spn_cc_compile_plan_t* plan;
  spn_path_t source;
  spn_event_subject_t subject;
  spn_dag_id_t libc;
} si_compile_t;

typedef struct {
  spn_rsp_style_t style;
  si_da(spn_dag_id_t) objects;
} si_rsp_t;

typedef struct {
  const spn_cc_t* cc;
  const spn_profile_info_t* profile;
  spn_dag_id_t libc;
  spn_cc_link_t link;
  sp_str_t name;
  sp_str_t triple;
} si_zig_warmup_t;

spn_err_t si_on_compile(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_compile_discover(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_archive(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_link(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_write_exports(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_write_rsp(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_write_libc(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_write_compile_commands(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_embed(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_user_node(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_publish(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_publish_file(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);
spn_err_t si_on_zig_warmup(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs);

#endif
