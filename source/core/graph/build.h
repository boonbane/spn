#ifndef SPN_GRAPH_BUILD_H
#define SPN_GRAPH_BUILD_H

#include "dag/types.h"
#include "unit/types.h"

typedef enum {
  SPN_DAG_BUILD_OBJECTS_DIRECT,
  SPN_DAG_BUILD_OBJECTS_RSP,
} spn_dag_build_objects_kind_t;

typedef struct {
  spn_dag_build_objects_kind_t kind;
  union {
    si_da(spn_dag_id_t) ids;
    spn_dag_id_t rsp;
  };
} spn_dag_build_objects_t;

spn_path_t         spn_target_unit_staged_path(sp_mem_t mem, spn_target_unit_t* unit);
spn_path_t         si_dag_get_artifact_path(spn_dag_t* g, spn_dag_id_t id);
si_da(spn_path_t)  si_dag_build_paths(sp_mem_t mem, spn_dag_t* g, si_da(spn_dag_id_t) ids);

#endif
