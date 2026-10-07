#ifndef SPN_TARGET_TARGET_H
#define SPN_TARGET_TARGET_H

#include "sp.h"
#include "spn/core.h"

#include "target/types.h"

spn_target_key_t  spn_target_key(sp_str_t name, spn_target_kind_t kind);
spn_target_info_t spn_target_info_new(sp_mem_t mem, sp_str_t name, spn_target_kind_t kind);

#endif
