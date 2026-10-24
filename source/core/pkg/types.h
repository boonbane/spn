#ifndef SPN_PKG_TYPES_H
#define SPN_PKG_TYPES_H

#include "sp.h"

#include "core/types.h"
#include "git/types.h"
#include "intern/types.h"
#include "paths/types.h"
#include "macro/macro.h"
#include "array/array.h"
#include "ordered_map.h"
#include "semver/types.h"
#include "target/types.h"
#include "toolchain/types.h"
#include "when/types.h"

typedef enum {
  SPN_PKG_SOURCE_ROOT,
  SPN_PKG_SOURCE_FILE,
  SPN_PKG_SOURCE_INDEX,
} spn_pkg_source_t;

typedef enum {
  SPN_MANIFEST_ROOT,
  SPN_MANIFEST_DEP,
} spn_manifest_role_t;

typedef enum {
  SPN_PKG_ROOT_NONE,
  SPN_PKG_ROOT_LOCAL,
  SPN_PKG_ROOT_GIT,
} spn_pkg_root_kind_t;

typedef struct {
  spn_pkg_root_kind_t kind;
  union {
    spn_path_t local;
    spn_git_checkout_id_t git;
  };
} spn_pkg_root_t;


typedef struct {
  sp_str_t namespace;
  sp_str_t name;
} spn_pkg_name_t;

SPN_PACK_PUSH
typedef struct {
  sp_intern_id_t qualified;
  sp_hash_t hash;
  spn_semver_t version;
} spn_pkg_id_t;
SPN_PACK_POP

_Static_assert(
  sizeof(spn_pkg_id_t) == sizeof(sp_intern_id_t) + sizeof(sp_hash_t) + sizeof(spn_semver_t),
  "spn_pkg_id_t is byte-hashed as a key; it must have no padding"
);

typedef struct {
  sp_intern_str_t qualified;
  spn_pkg_source_t source;
  spn_dep_kind_t kind;
  bool private;
  spn_when_t when;
  spn_when_t options;
  union {
    struct { spn_semver_range_t range; } index;
    struct { spn_path_t path; } file;
  };
} spn_requested_dep_t;


typedef struct {
  sp_intern_str_t name;
  sp_opt(spn_linkage_t) kind;
  spn_when_t options;
  bool defaults_declined;
} spn_pkg_config_t;

typedef si_om(sp_intern_id_t, spn_pkg_config_t) spn_pkg_config_map_t;

typedef struct {
  sp_intern_str_t qualified;
  spn_git_patch_set_t set;
} spn_pkg_patch_t;

typedef sp_intern_id_t spn_index_id_t;

typedef si_om(spn_profile_id_t, spn_profile_decl_t) spn_profile_map_t;
typedef si_om(spn_index_id_t, spn_index_info_t) spn_index_map_t;
typedef si_om(spn_toolchain_id_t, spn_toolchain_decl_t) spn_toolchain_map_t;

struct spn_manifest {
  sp_str_t namespace;
  sp_intern_str_t name;
  sp_intern_str_t qualified;
  sp_str_t repo;
  sp_str_t author;
  sp_str_t maintainer;
  spn_semver_t version;
  spn_pkg_root_t upstream;
  si_om(spn_target_id_t, spn_target_info_t) targets;
  spn_profile_map_t profiles;
  spn_index_map_t indexes;
  si_da(spn_requested_dep_t) deps;
  spn_pkg_config_map_t config;
  si_da(spn_pkg_patch_t) patches;
  spn_option_map_t options;
  spn_toolchain_map_t toolchains;
  spn_gated_path_list_t include;
  spn_gated_list_t define;
  spn_gated_list_t system_deps;
  si_da(spn_gated_publish_t) publish;
  struct {
    spn_gated_list_t frameworks;
    spn_os_version_t min_os;
  } macos;
};

struct spn_pkg {
  const spn_manifest_t* manifest;
  spn_target_eval_map_t targets;
  si_da(spn_path_t) include;
  si_da(sp_str_t) define;
  si_da(sp_str_t) public_define;
  si_da(sp_str_t) system_deps;
  si_da(spn_publish_t) publish;
  struct {
    si_da(spn_path_t) include;
  } configured;
  struct {
    si_da(sp_str_t) frameworks;
  } macos;
};

#endif
