#ifndef SPN_INDEX_RELEASE_H
#define SPN_INDEX_RELEASE_H

#include "sp.h"
#include "spn/core.h"
#include "index/types.h"

spn_err_t spn_index_release_from_manifest(sp_mem_t mem, const si_pkg_info_t* manifest, spn_pkg_root_t published, spn_index_release_t* release, sp_str_t* dep);

#endif
