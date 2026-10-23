#ifndef SPN_DAG_GLOB_H
#define SPN_DAG_GLOB_H

#include "sp.h"
#include "spn/core.h"
#include "dag/types.h"

spn_err_t         spn_dag_glob(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t pattern, spn_dag_glob_result_t* result);
spn_dag_glob_it_t spn_dag_glob_it_new(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t pattern);
bool              spn_dag_glob_it_next(spn_dag_glob_it_t* it);
void              spn_dag_glob_it_deinit(spn_dag_glob_it_t* it);

#endif
