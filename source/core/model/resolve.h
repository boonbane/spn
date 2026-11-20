#ifndef SPN_MODEL_RESOLVE_H
#define SPN_MODEL_RESOLVE_H

#include "sp.h"
#include "spn/core.h"
#include "core/types.h"
#include "index/types.h"
#include "lock/types.h"

spn_err_t si_resolve(spn_op_t* op, const spn_lock_t* lock, si_da(si_index_place_t) placed);

#endif
