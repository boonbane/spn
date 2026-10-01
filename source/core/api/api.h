#ifndef SPN_API_H
#define SPN_API_H

#include "sp.h"
#include "spn/core.h"

#include "core/types.h"
#include "paths/types.h"

spn_pkg_unit_t* spn_api_unit(const void* opaque);
spn_path_t      spn_api_tree_path(spn_pkg_unit_t* unit, const c8* fn, const c8* path);
bool            spn_api_path_rejected(spn_pkg_unit_t* unit, const c8* fn, sp_str_t path);
spn_path_t      spn_api_dir_path(spn_pkg_unit_t* unit, spn_dir_t dir);
sp_str_t        spn_api_dir(spn_pkg_unit_t* unit, spn_dir_t dir);
s32             spn_api_copy(sp_path_t from, sp_path_t to);

#endif
