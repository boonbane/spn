#ifndef SPN_DAG_RUN_H
#define SPN_DAG_RUN_H

#include "sp.h"
#include "spn/core.h"
#include "dag/types.h"
#include "thread_pool/types.h"

const spn_dag_artifact_state_t* spn_dag_run_artifact(const spn_dag_run_t* run, spn_dag_id_t id);

spn_err_t    spn_dag_run_begin(spn_dag_run_t* run, sp_mem_t mem);
spn_dag_id_t spn_dag_run_next(spn_dag_run_t* run);
void         spn_dag_run_attempt(spn_dag_run_t* run, spn_dag_id_t id);
void         spn_dag_run_complete(spn_dag_run_t* run, spn_dag_id_t id);
spn_err_t    spn_dag_run_end(spn_dag_run_t* run);
spn_err_t    spn_dag_run(spn_dag_run_t* run, spn_thread_pool_t* pool);

#endif
