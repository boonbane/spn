#ifndef SPN_API_H
#define SPN_API_H

#include "sp.h"
#include "spn/core.h"

#include "core/types.h"
#include "paths/types.h"

spn_pkg_unit_t* spn_api_unit(const void* opaque);
spn_path_t      spn_api_dir_path(spn_pkg_unit_t* unit, spn_dir_t dir);

#endif
