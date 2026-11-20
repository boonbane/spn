#ifndef SPN_INDEX_CACHE_H
#define SPN_INDEX_CACHE_H

#include "sp.h"
#include "spn/core.h"
#include "index/types.h"
#include "spn/errors.h"

void si_index_cache_init(spn_index_cache_t* cache, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, si_da(si_index_place_t) indexes);
spn_err_t si_index_cache_get_package(spn_index_cache_t* cache, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err);

#endif
