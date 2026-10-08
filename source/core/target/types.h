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
  spn_pkg_unit_t* pkg;
  sp_da(spn_target_unit_t*) targets;
  bool private;
  bool links_code;
} spn_closure_entry_t;

typedef struct {
  spn_pkg_unit_t* pkg;
  spn_target_unit_t* lib;
  bool private;
} spn_link_lib_t;



typedef struct {
  bool source;
  bool shared;
  bool static_lib;
  bool object;
} spn_linkage_set_t;

typedef sp_opt(spn_linkage_t) sp_opt_spn_linkage_t;

SPN_PACK_PUSH
typedef struct {
  sp_intern_id_t name;
  spn_target_kind_t kind;
} spn_target_key_t;
SPN_PACK_POP

_Static_assert(
  sizeof(spn_target_key_t) == sizeof(sp_intern_id_t) + sizeof(spn_target_kind_t),
  "spn_target_key_t is byte-hashed as a key; it must have no padding"
);

struct spn_target_info {
  sp_str_t name;
  spn_target_kind_t kind;
  spn_linkage_set_t linkages;
  bool no_link; // @spader A hack for libtcc1.a (building an unlinked library)
  si_da(spn_source_t) source;
  si_da(spn_path_t) headers;
  si_da(spn_path_t) include;
  si_da(sp_str_t) define;
  si_da(sp_str_t) flags;
  si_da(sp_str_t) link_flags;
  si_da(spn_path_t) linker_script;
  si_da(sp_str_t) system_deps;
  si_da(sp_str_t) deps;
  si_da(spn_embed_t) embed;
  spn_cxx_options_t cxx;
  struct {
    si_da(spn_path_t) include;
  } configured;
  struct {
    si_da(sp_str_t) frameworks;
    spn_os_version_t min_os;
  } macos;
  struct {
    spn_win_subsystem_t subsystem;
  } windows;
  struct {
    si_da(spn_gated_source_t) source;
    spn_gated_path_list_t headers;
    spn_gated_path_list_t include;
    spn_gated_list_t define;
    spn_gated_list_t flags;
    spn_gated_list_t link_flags;
    spn_gated_path_list_t linker_script;
    spn_gated_list_t system_deps;
    spn_gated_list_t deps;
    spn_gated_list_t frameworks;
    sp_da(spn_gated_embed_t) embed;
  } gated;
};

typedef si_om(spn_target_key_t, spn_target_info_t) spn_target_map_t;

#endif
