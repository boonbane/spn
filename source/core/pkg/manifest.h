#ifndef SPN_PKG_MANIFEST_H
#define SPN_PKG_MANIFEST_H

#include "sp.h"
#include "spn/core.h"

#include "paths/types.h"
#include "pkg/types.h"

spn_manifest_t           spn_manifest_new(sp_str_t name);
spn_err_t                spn_manifest_add_target(sp_mem_t mem, spn_manifest_t* manifest, sp_str_t name, spn_target_kind_t kind, spn_target_decl_t** decl);
spn_pkg_root_t           spn_manifest_upstream(const spn_manifest_t* manifest);
sp_hash_t                spn_manifest_hash_platform(const spn_manifest_t* manifest, const spn_profile_info_t* profile);

#endif
