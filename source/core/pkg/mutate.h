#ifndef SPN_PKG_MUTATE_H
#define SPN_PKG_MUTATE_H

#include "sp.h"
#include "spn/core.h"
#include "pkg/types.h"
#include "target/types.h"

void                spn_pkg_add_include(sp_mem_t mem, spn_pkg_info_t* pkg, spn_path_t path);
void                spn_pkg_add_define(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t define);
void                spn_pkg_add_system_dep(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t dep);
spn_err_t           spn_pkg_add_target(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t name, spn_target_kind_t kind, spn_target_info_t** out);

#endif
