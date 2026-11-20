#ifndef SPN_INDEX_TIP_H
#define SPN_INDEX_TIP_H

#include "sp.h"
#include "spn/core.h"
#include "index/types.h"

typedef struct {
  sp_str_t rev;
  bool fetch;
  bool held;
} si_index_tip_t;

si_index_tip_t si_index_tip_plan(sp_mem_t mem, const spn_index_info_t* index);
void           si_index_tip_save(const spn_index_info_t* index, sp_str_t rev);
void           si_index_tip_hold(const spn_index_info_t* index);
void           si_index_tip_expire(const spn_index_info_t* index);

#endif
