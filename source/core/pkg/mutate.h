#ifndef SPN_PKG_MUTATE_H
#define SPN_PKG_MUTATE_H

#include "sp.h"
#include "spn/core.h"
#include "pkg/types.h"
#include "target/types.h"

void                spn_pkg_init(spn_pkg_info_t* pkg, sp_str_t name);
void                spn_pkg_set_name(spn_pkg_info_t* pkg, const c8* name);
void                spn_pkg_set_name_ex(spn_pkg_info_t* pkg, sp_str_t name);
void                spn_pkg_set_repo(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* repo);
void                spn_pkg_set_repo_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t repo);
void                spn_pkg_set_author(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* author);
void                spn_pkg_set_author_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t author);
void                spn_pkg_set_maintainer(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* maintainer);
void                spn_pkg_set_maintainer_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t maintainer);
void                spn_pkg_add_include(sp_mem_t mem, spn_pkg_info_t* pkg, spn_path_t path);
void                spn_pkg_add_define(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* define);
void                spn_pkg_add_define_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t define);
void                spn_pkg_add_system_dep(sp_mem_t mem, spn_pkg_info_t* pkg, const c8* dep);
void                spn_pkg_add_system_dep_ex(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t dep);
spn_err_t           spn_pkg_add_target(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t name, spn_target_kind_t kind, spn_target_info_t** out);

#endif
