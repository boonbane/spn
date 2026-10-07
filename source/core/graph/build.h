#ifndef SPN_GRAPH_BUILD_H
#define SPN_GRAPH_BUILD_H

#include "dag/types.h"
#include "unit/types.h"

typedef struct {
  spn_build_unit_t* unit;
  spn_dag_id_t libc;
} spn_dag_build_ctx_t;

spn_path_t         spn_target_unit_staged_path(sp_mem_t mem, spn_target_unit_t* unit);
spn_path_t         spn_target_exports_path(sp_mem_t mem, spn_target_unit_t* unit);
spn_profile_info_t spn_dag_build_profile(spn_dag_t* g, const spn_dag_build_ctx_t* build);

#endif
