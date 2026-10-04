#ifndef SPN_STAGE_TYPES_H
#define SPN_STAGE_TYPES_H

#include "sp.h"
#include "str/str.h"

typedef struct {
  sp_str_t owner;
  sp_str_t path;
} spn_stage_record_t;

typedef struct {
  sp_str_line_it_t line;
  spn_stage_record_t record;
} spn_stage_it_t;

#endif
