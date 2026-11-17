#ifndef SPN_TOML_H
#define SPN_TOML_H

#include "sp.h"
#include "toml.h"

typedef enum {
  SPN_TOML_VALUE_KIND_NONE,
  SPN_TOML_VALUE_KIND_SCALAR,
  SPN_TOML_VALUE_KIND_ARRAY,
  SPN_TOML_VALUE_KIND_TABLE,
} spn_toml_value_kind_t;

toml_table_t*     spn_toml_parse(sp_path_t path);
toml_table_t*     spn_toml_parse_str_diag(sp_mem_t mem, sp_str_t content, sp_str_t* diag);

#define spn_toml_for(tbl, it, key) \
  for (s32 it = 0, SP_UNIQUE_ID() = 0; it < toml_table_len(tbl) && (key = toml_table_key(tbl, it, &SP_UNIQUE_ID())); it++)

#endif
