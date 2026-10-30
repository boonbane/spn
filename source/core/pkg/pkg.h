#ifndef SPN_PKG_PKG_H
#define SPN_PKG_PKG_H

#include "sp.h"
#include "spn/core.h"

#include "paths/types.h"
#include "pkg/types.h"
#include "when/types.h"

si_pkg_t          spn_pkg_eval(sp_mem_t mem, const si_pkg_info_t* manifest, const spn_path_roots_t* roots, spn_tree_roots_t trees, spn_when_env_t* env);
si_target_t*      spn_pkg_get_target(si_pkg_t* pkg, sp_str_t name, spn_target_kind_t kind);
si_target_t*      spn_pkg_get_script(si_pkg_t* pkg, spn_target_kind_t kind);
bool              spn_pkg_has_scripts(si_pkg_t* pkg);

#endif
