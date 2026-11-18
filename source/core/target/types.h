#ifndef SPN_TARGET_TYPES_H
#define SPN_TARGET_TYPES_H

#include "sp.h"
#include "spn/core.h"

#include "core/types.h"
#include "compiler/types.h"
#include "intern/types.h"
#include "macro/macro.h"
#include "paths/types.h"
#include "array/array.h"
#include "ordered_map.h"
#include "when/types.h"

typedef enum {
  SPN_EMBED_FILE,
  SPN_EMBED_DIR,
} spn_embed_kind_t;

typedef struct {
  sp_str_t data;
  sp_str_t size;
} spn_embed_types_t;

typedef struct {
  spn_embed_kind_t kind;
  spn_path_t path;
  sp_str_t dest;
  spn_embed_types_t types;
} spn_embed_t;

typedef struct {
  spn_embed_kind_t kind;
  sp_str_t path;
  spn_tree_t tree;
  sp_str_t dest;
  spn_embed_types_t types;
  spn_when_t when;
} spn_gated_embed_t;

typedef enum {
  SPN_SOURCE_FILE,
  SPN_SOURCE_GLOB,
} spn_source_kind_t;

typedef struct {
  spn_source_kind_t kind;
  spn_path_t path;
} spn_source_t;

typedef struct {
  spn_source_kind_t kind;
  sp_str_t path;
  spn_tree_t tree;
  spn_when_t when;
} spn_gated_source_t;

typedef struct {
  sp_intern_str_t name;
  spn_when_t when;
} spn_gated_dep_t;

typedef enum {
  SPN_REQUIREMENT_LIB,
  SPN_REQUIREMENT_FRAMEWORK,
  SPN_REQUIREMENT_HOST,
} spn_requirement_kind_t;

typedef struct {
  spn_requirement_kind_t kind;
  sp_str_t name;
} spn_requirement_t;

typedef struct {
  spn_requirement_kind_t kind;
  sp_str_t name;
  spn_when_t when;
} spn_gated_requirement_t;

typedef struct {
  spn_publish_root_t root;
  spn_source_t source;
  sp_str_t dest;
} spn_publish_t;

typedef struct {
  spn_publish_root_t root;
  spn_gated_source_t source;
  sp_str_t dest;
} spn_gated_publish_t;

typedef struct {
  spn_path_t path;
  spn_path_t stub;
} spn_prebuilt_t;

typedef struct {
  sp_str_t path;
  sp_str_t stub;
  spn_tree_t tree;
  spn_when_t when;
} spn_gated_prebuilt_t;

typedef struct {
  bool shared;
  bool static_lib;
} spn_linkage_set_t;

typedef sp_opt(spn_linkage_t) sp_opt_spn_linkage_t;

SPN_PACK_PUSH
typedef struct {
  sp_intern_id_t name;
  spn_target_kind_t kind;
} si_target_id_t;
SPN_PACK_POP

_Static_assert(
  sizeof(si_target_id_t) == sizeof(sp_intern_id_t) + sizeof(spn_target_kind_t),
  "spn_target_id_t is byte-hashed as a key; it must have no padding"
);

struct si_target_info {
  si_target_id_t id;
  sp_intern_str_t name;
  spn_target_kind_t kind;
  spn_linkage_set_t linkages;
  bool no_link; // @spader A hack for libtcc1.a (building an unlinked library)
  spn_cxx_options_t cxx;
  si_da(spn_gated_source_t) source;
  si_da(spn_gated_publish_t) publish;
  spn_gated_path_list_t include;
  spn_gated_list_t define;
  spn_gated_list_t flags;
  spn_gated_list_t link_flags;
  spn_gated_path_list_t linker_script;
  si_da(spn_gated_prebuilt_t) prebuilt;
  si_da(spn_gated_requirement_t) requires;
  si_da(spn_gated_dep_t) deps;
  si_da(spn_gated_embed_t) embed;
  struct {
    spn_os_version_t min_os;
  } macos;
  struct {
    spn_win_subsystem_t subsystem;
  } windows;
};

struct si_target {
  const si_target_info_t* info;
  si_da(spn_source_t) source;
  si_da(spn_publish_t) publish;
  si_da(spn_path_t) include;
  si_da(sp_str_t) define;
  si_da(sp_str_t) flags;
  si_da(sp_str_t) link_flags;
  si_da(spn_path_t) linker_script;
  si_da(spn_prebuilt_t) prebuilt;
  si_da(spn_requirement_t) requires;
  si_da(sp_intern_str_t) deps;
  si_da(spn_embed_t) embed;
};

typedef si_om(si_target_id_t, si_target_info_t) spn_target_info_map_t;
typedef si_om(si_target_id_t, si_target_t) spn_target_map_t;

#endif
