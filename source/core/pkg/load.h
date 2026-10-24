#ifndef SPN_PKG_LOAD_H
#define SPN_PKG_LOAD_H

#include "sp.h"
#include "spn/core.h"
#include "spn/errors.h"
#include "paths/types.h"
#include "pkg/types.h"

spn_err_t spn_pkg_load(sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_path_t path, si_pkg_info_t* pkg, spn_err_union_t* err);

#endif
