#ifndef SPN_INDEX_TIP_H
#define SPN_INDEX_TIP_H

#include "sp.h"
#include "spn/core.h"
#include "spn/errors.h"
#include "external/git.h"
#include "index/types.h"

typedef struct {
  sp_str_t rev;
  bool fetch;
  bool held;
} si_index_tip_t;

si_index_tip_t si_index_tip_plan(sp_mem_t mem, const spn_index_info_t* index, bool force);
spn_err_t      si_index_tip_fetch(sp_mem_t mem, const spn_index_info_t* index, sp_atomic_s32_t* cancelled, sp_str_t* rev, spn_git_failure_t* failure);

#endif
