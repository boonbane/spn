#ifndef SPN_INDEX_DIR_H
#define SPN_INDEX_DIR_H

#include "sp.h"
#include "spn/core.h"
#include "index/types.h"
#include "spn/errors.h"

bool      spn_index_dir_has_packages(sp_mem_t mem, spn_index_info_t* index);
spn_err_t si_index_dir_get_package(const si_index_place_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err);

#endif
