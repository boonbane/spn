#ifndef SPN_STAGE_TYPES_H
#define SPN_STAGE_TYPES_H

#include "sp.h"
#include "spn/core.h"
#include "dag/types.h"
#include "paths/types.h"

typedef struct {
  sp_str_t key;
  spn_path_t to;
  spn_dag_digest_t digest;
  spn_path_t location;
} spn_stage_t;

typedef struct {
  spn_err_t err;
  spn_path_t path;
} spn_stage_diag_t;

#endif
