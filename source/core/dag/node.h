#ifndef SPN_DAG_NODE_H
#define SPN_DAG_NODE_H

#include "sp.h"
#include "spn/core.h"
#include "dag/types.h"

spn_path_t si_dag_resolve_input(const si_dag_ctx_t* c, spn_dag_id_t id);
spn_path_t si_dag_resolve_output(const si_dag_ctx_t* c, spn_dag_id_t id);
spn_path_t si_dag_canonicalize_path(const si_dag_ctx_t* c, spn_path_t path);
void       si_dag_observe(const si_dag_ctx_t* c, spn_dag_obs_t obs);
void       si_dag_add_warm(const si_dag_ctx_t* c, s64 delta);

#endif
