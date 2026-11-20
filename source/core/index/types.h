#ifndef SPN_INDEX_TYPES_H
#define SPN_INDEX_TYPES_H

#include "intern/types.h"
#include "paths/types.h"
#include "core/types.h"
#include "external/net.h"
#include "spn/types.h"
#include "sp_om/sp_om.h"
#include "array/array.h"
#include "pkg/types.h"
#include "semver/types.h"

typedef sp_intern_id_t spn_index_id_t;

typedef enum {
  SPN_INDEX_DEP_NORMAL,
  SPN_INDEX_DEP_BUILD,
} spn_index_dep_kind_t;

typedef struct {
  spn_index_dep_kind_t kind;
  bool public;
  sp_intern_str_t name;
  spn_semver_range_t range;
  spn_when_t when;
  spn_when_t options;
} spn_index_dep_t;

typedef struct {
  sp_str_t manifest;
} spn_index_rel_paths_t;

typedef struct {
  sp_str_t name;
  sp_da(spn_linkage_t) linkages;
} spn_index_target_t;

typedef struct {
  sp_intern_str_t name;
  spn_semver_t version;

  bool yanked;
  bool follows;

  spn_pkg_root_t source;
  spn_pkg_root_t manifest;
  spn_index_rel_paths_t paths;

  sp_da(spn_index_dep_t) deps;
  sp_da(spn_index_target_t) targets;
  spn_option_map_t options;
} spn_index_release_t;

typedef struct {
  sp_da(spn_index_release_t) releases;
} spn_index_pkg_t;

struct spn_index_info {
  spn_index_id_t id;
  sp_intern_str_t name;
  spn_index_kind_t kind;
  spn_index_protocol_t protocol;
  spn_index_format_t format;
  union {
    struct {
      sp_str_t url;
      sp_str_t rev;
      sp_str_t publish_url;
    } git;
    struct {
      sp_str_t url;
    } http;
    struct {
      spn_path_t path;
    } dir;
  };
  struct {
    sp_path_t at;
    sp_str_t dir;
    sp_path_t lock;
  } location;
  u32 refresh;
  spn_git_remote_t remote;
  spn_net_backoff_t backoff;
};

typedef si_om(spn_index_id_t, spn_index_info_t) spn_index_map_t;

typedef struct {
  spn_index_format_t format;
  sp_path_t at;
  sp_str_t dir;
  struct {
    sp_str_t url;
    sp_str_t rev;
    sp_str_t dir;
  } git;
} si_index_place_t;

typedef struct {
  sp_mem_t mem;
  sp_intern_t* intern;
  const spn_path_roots_t* roots;
  si_da(si_index_place_t) indexes;
  sp_ht(sp_intern_id_t, spn_index_pkg_t*) packages;
  u32 consulted;
} spn_index_cache_t;

#endif
