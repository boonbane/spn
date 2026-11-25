#ifndef spn_compiler_stdinc_h
#define spn_compiler_stdinc_h

#include "sp.h"
#include "spn/core.h"

#include "array/array.h"

spn_err_t si_cc_parse_search_list(sp_mem_t mem, sp_str_t content, sp_str_t builtin, si_da(sp_str_t)* args);

#endif
