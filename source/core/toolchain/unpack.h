#ifndef SPN_TOOLCHAIN_UNPACK_H
#define SPN_TOOLCHAIN_UNPACK_H

#include "sp.h"
#include "spn/core.h"

typedef struct {
  spn_unpack_reason_t reason;
  sp_str_t entry;
} spn_unpack_failure_t;

spn_err_t spn_artifact_unpack(sp_mem_t mem, sp_path_t archive, sp_path_t root, spn_unpack_failure_t* failure);

#endif
