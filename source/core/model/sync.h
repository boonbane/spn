#ifndef SPN_MODEL_SYNC_H
#define SPN_MODEL_SYNC_H

#include "sp.h"
#include "spn/core.h"
#include "core/types.h"
#include "toolchain/types.h"
#include "unit/types.h"

spn_err_t spn_sync(spn_op_t* op, bool* reresolve);
spn_err_t spn_sync_toolchain(spn_toolchain_store_t* store, spn_toolchain_unit_t* unit);

#endif
