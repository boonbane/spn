#ifndef SPN_MODEL_SYNC_H
#define SPN_MODEL_SYNC_H

#include "sp.h"
#include "spn/core.h"
#include "core/types.h"
#include "lock/types.h"
#include "toolchain/types.h"
#include "unit/types.h"

spn_err_t spn_resolve_and_sync(spn_op_t* op);
spn_err_t spn_resolve_and_sync_pins(spn_op_t* op, const spn_lock_t* held, const spn_lock_t* pins, spn_lock_t* lock);
spn_err_t spn_sync_toolchain(spn_toolchain_store_t* store, spn_toolchain_unit_t* unit, sp_atomic_s32_t* cancelled);

#endif
