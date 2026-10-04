#ifndef SPN_STAGE_TYPES_H
#define SPN_STAGE_TYPES_H

#include "sp.h"
#include "str/str.h"

typedef enum {
  SPN_STAGE_DECLARER_NONE,
  SPN_STAGE_DECLARER_STAGE,
  SPN_STAGE_DECLARER_EXE,
  SPN_STAGE_DECLARER_COMPILE_COMMANDS,
} spn_stage_declarer_t;

#define spn_stage_declarer_bit(declarer) (1u << (declarer))

typedef struct {
  spn_stage_declarer_t declarer;
  sp_str_t owner;
  sp_str_t path;
} spn_stage_record_t;

typedef struct {
  sp_str_line_it_t line;
  spn_stage_record_t record;
} spn_stage_it_t;

#endif
