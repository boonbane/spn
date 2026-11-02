#ifndef SPN_STAGE_STAGE_H
#define SPN_STAGE_STAGE_H

#include "sp.h"
#include "array/array.h"
#include "dag/types.h"
#include "stage/types.h"

spn_stage_diag_t spn_stage_apply(sp_mem_t scratch, const spn_path_roots_t* roots, spn_dag_file_cache_t* files, spn_path_t manifest, si_da(sp_str_t) declared, si_da(spn_stage_t) rows);
spn_stage_diag_t spn_stage_clean(sp_mem_t scratch, const spn_path_roots_t* roots, spn_path_t manifest);

#endif
