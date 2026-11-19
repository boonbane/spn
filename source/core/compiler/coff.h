#ifndef SPN_COMPILER_COFF_H
#define SPN_COMPILER_COFF_H

#include "sp.h"
#include "spn/core.h"

typedef struct {
  sp_str_t name;
  bool data;
} spn_coff_symbol_t;

typedef struct {
  sp_str_t object;
  u64 sections;
  u64 symbols;
  u64 strings;
  u32 num_sections;
  u32 num_symbols;
  u32 strings_len;
  u32 record;
  u32 index;
  spn_err_t err;
} spn_coff_parser_t;

spn_err_t spn_coff_init(spn_coff_parser_t* coff, sp_str_t object);
bool      spn_coff_next(spn_coff_parser_t* coff, spn_coff_symbol_t* symbol);

#endif
