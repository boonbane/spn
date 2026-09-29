#ifndef SPN_STAGE_TYPES_H
#define SPN_STAGE_TYPES_H

#include "sp.h"

typedef struct {
  sp_str_t owner;
  sp_str_t path;
} spn_stage_record_t;

typedef struct {
  sp_da(spn_stage_record_t) records;
  sp_da(sp_str_t) dropped;
} spn_stage_plan_t;

#endif
