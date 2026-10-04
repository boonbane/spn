#ifndef SPN_GRAPH_DAG_H
#define SPN_GRAPH_DAG_H

#include "dag/dag.h"
#include "core/types.h"
#include "thread_pool/types.h"
#include "unit/types.h"

typedef struct {
  spn_dag_id_t action;
  spn_dag_id_t object;
} spn_dag_object_ids_t;

typedef struct {
  spn_dag_id_t action;
  spn_dag_id_t output;
  struct {
    spn_dag_id_t action;
    spn_dag_id_t object;
    spn_dag_id_t header;
  } embed;
  spn_dag_id_t exports;
} spn_dag_target_ids_t;

struct spn_dag_build_t {
  spn_session_t* session;
  sp_mem_t mem;
  spn_dag_t* graph;

  struct {
    sp_ht(spn_pkg_unit_t*, sp_da(spn_dag_id_t)) user_outputs;
    sp_ht(spn_path_t, spn_dag_id_t) stamps;
    sp_ht(spn_target_unit_t*, spn_dag_target_ids_t) targets;
    sp_ht(spn_compile_unit_t*, spn_dag_object_ids_t) objects;
    sp_ht(spn_dag_digest_t, spn_dag_id_t) warm;
    sp_ht(spn_dag_digest_t, spn_dag_id_t) libc;
  } ids;
  spn_dag_id_t compile_commands;
  spn_dag_action_cache_t actions;
  spn_dag_obs_table_t discovery;
  spn_dag_store_t store;
  spn_thread_pool_t pool;
  spn_dag_env_t env;
  spn_dag_progress_t progress;
  spn_dag_stats_t stats;
  spn_err_t result;
  sp_tm_timer_t timer;
};

spn_err_t        spn_dag_build_session(spn_op_t* op);
spn_dag_build_t* spn_dag_build_new(spn_op_t* op);
spn_err_t        spn_dag_build_run(spn_dag_build_t* b, u32 workers);
spn_err_t        spn_dag_build_add_target(spn_dag_build_t* b, spn_target_unit_t* target, const spn_target_plan_t* plan);

#endif
