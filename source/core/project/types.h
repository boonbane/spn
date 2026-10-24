#ifndef SPN_PROJECT_TYPES_H
#define SPN_PROJECT_TYPES_H

#include "sp.h"
#include "spn/core.h"

#include "index/types.h"
#include "lock/types.h"
#include "pkg/types.h"
#include "profile/types.h"
#include "toolchain/types.h"

struct spn_project_t {
  struct {
    spn_path_t manifest;
    spn_path_t lock;
  } paths;
  si_pkg_info_t package;
  spn_profile_map_t profiles;
  spn_index_map_t indexes;
  spn_toolchain_map_t toolchains;
  spn_pkg_config_map_t config;
  si_da(spn_pkg_patch_t) patches;
  sp_opt(spn_lock_file_t) lock;
};

#endif
