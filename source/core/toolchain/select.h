#ifndef SPN_TOOLCHAIN_SELECT_H
#define SPN_TOOLCHAIN_SELECT_H

#include "toolchain/types.h"

spn_err_t       si_toolchain_select(spn_toolchain_catalog_t* catalog, spn_toolchain_query_t query, spn_toolchain_selection_t* selection);
sp_da(sp_str_t) spn_toolchain_host_candidates(sp_mem_t mem, spn_toolchain_catalog_t* catalog);
sp_da(sp_str_t) spn_toolchain_exceptions_candidates(sp_mem_t mem, spn_toolchain_catalog_t* catalog, spn_triple_t target);

#endif
