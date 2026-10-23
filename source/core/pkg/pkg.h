#ifndef SPN_PKG_PKG_H
#define SPN_PKG_PKG_H

#include "sp.h"
#include "spn/core.h"

#include "paths/types.h"
#include "pkg/types.h"
#include "when/types.h"

spn_pkg_t          spn_pkg_eval(sp_mem_t mem, const spn_manifest_t* manifest, const spn_path_roots_t* roots, spn_tree_roots_t trees, spn_when_env_t* env);
spn_target_eval_t* spn_pkg_get_target(spn_pkg_t* pkg, sp_str_t name, spn_target_kind_t kind);
bool               spn_pkg_has_scripts(const spn_pkg_t* pkg);

void spn_pkg_add_include(sp_mem_t mem, spn_pkg_t* pkg, spn_path_t path);
void spn_pkg_add_define(sp_mem_t mem, spn_pkg_t* pkg, sp_str_t define);
void spn_pkg_add_system_dep(sp_mem_t mem, spn_pkg_t* pkg, sp_str_t dep);

#endif
