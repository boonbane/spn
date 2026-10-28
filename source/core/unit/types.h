#ifndef SPN_UNIT_TYPES_H
#define SPN_UNIT_TYPES_H

#include "core/types.h"
#include "compiler/types.h"
#include "array/array.h"
#include "dag/types.h"
#include "paths/types.h"
#include "sp.h"
#include "spn/core.h"
#include "spn/types.h"
#include "intern/types.h"
#include "pkg/types.h"
#include "macro/macro.h"
#include "profile/types.h"
#include "target/types.h"

struct spn_build_unit_t {
  spn_build_id_t id;
  spn_profile_info_t profile;
  spn_toolchain_unit_t* toolchain;
  sp_da(spn_path_t) include;
  sp_da(sp_str_t) define;
  sp_da(spn_pkg_unit_t*) packages;
  struct {
    spn_path_t root;
  } paths;
};

#define spn_dep_kind_bit(kind) (1u << (kind))

SPN_PACK_PUSH
typedef struct {
  spn_pkg_id_t pkg;
  spn_build_id_t build;
} spn_pkg_unit_id_t;

typedef struct {
  spn_pkg_unit_id_t pkg;
  si_target_id_t target;
} spn_target_unit_id_t;

typedef struct {
  spn_target_unit_id_t target;
  spn_path_id_t source;
} spn_compile_unit_id_t;
SPN_PACK_POP

_Static_assert(
  sizeof(spn_pkg_unit_id_t) == sizeof(spn_pkg_id_t) + sizeof(spn_build_id_t),
  "spn_pkg_unit_id_t is byte-hashed as a key; it must have no padding"
);
_Static_assert(
  sizeof(spn_target_unit_id_t) == sizeof(spn_pkg_unit_id_t) + sizeof(si_target_id_t),
  "spn_target_unit_id_t is byte-hashed as a key; it must have no padding"
);
_Static_assert(
  sizeof(spn_compile_unit_id_t) == sizeof(spn_target_unit_id_t) + sizeof(spn_path_id_t),
  "spn_compile_unit_id_t is byte-hashed as a key; it must have no padding"
);

typedef struct {
  spn_target_unit_t* target;
  spn_path_t path;
} spn_stage_entry_t;

typedef struct {
  spn_stage_entry_t exe;
  sp_da(spn_stage_entry_t) libs;
} spn_stage_closure_t;

typedef struct {
  spn_build_unit_t* build;
  spn_pkg_unit_t* root;
  struct {
    u32 kinds;
    si_om(sp_intern_id_t, sp_intern_str_t) names;
  } selection;
  si_da(spn_target_unit_t*) roots;
  sp_da(spn_stage_closure_t) staged;
} spn_build_plan_t;

typedef struct {
  spn_pkg_unit_t* unit;
  spn_dep_kind_t kind;
  bool private;
} spn_pkg_dep_t;

typedef struct {
  spn_pkg_unit_t* pkg;
  u32 index;
} spn_node_ref_t;

typedef struct {
  spn_dir_t dir;
  sp_str_t sub;
  spn_dag_artifact_kind_t kind;
  spn_path_t path;
} spn_user_output_t;

struct spn_user_node_t {
  spn_pkg_unit_t* unit;
  sp_str_t tag;
  sp_str_t fn;
  si_da(spn_path_t) inputs;
  si_da(spn_user_output_t) outputs;
  si_da(spn_node_ref_t) deps;
};

typedef struct {
  spn_compile_unit_id_t id;
  spn_target_unit_t* target;
  spn_lang_t lang;
  sp_str_t name;
  spn_path_t file;
  spn_cc_compile_plan_t plan;
} spn_compile_unit_t;

typedef enum {
  SPN_LINK_ROLE_ARCHIVE,
  SPN_LINK_ROLE_SHARED,
  SPN_LINK_ROLE_WHOLE_ARCHIVE,
} spn_link_role_t;

typedef struct {
  spn_target_unit_t* lib;
  spn_link_role_t role;
} spn_link_dep_t;

typedef struct {
  spn_cc_link_t cc;
  si_da(spn_link_dep_t) deps;
  spn_cc_link_plan_t plan;
} spn_link_plan_t;

struct spn_target_unit {
  spn_target_unit_id_t id;
  spn_pkg_unit_t* pkg;
  si_target_t* target;
  spn_cc_output_kind_t kind;
  spn_linkage_t lib_kind;
  sp_str_t file_name;

  sp_da(spn_compile_unit_t*) objects;
  sp_da(spn_target_unit_t*) deps;
  spn_link_plan_t link;
  spn_cc_archive_plan_t archive;
};

struct spn_pkg_unit_t {
  spn_pkg_unit_id_t id;
  spn_build_unit_t* build;
  spn_session_t* session;
  si_pkg_t pkg;
  spn_pkg_source_t source;
  u32 kinds;
  sp_hash_t fingerprint;

  spn_pkg_unit_t* metaprogram;
  struct {
    spn_target_unit_t* configure;
    spn_target_unit_t* build;
  } scripts;

  sp_da(spn_pkg_dep_t) deps;
  si_da(spn_target_unit_t*) libs;
  sp_da(spn_target_unit_t*) targets;
  sp_da(spn_user_node_t) user_nodes;

  struct {
    spn_tree_roots_t roots;
    spn_path_t work;
    spn_path_t store;
    spn_path_t include;
    spn_path_t share;
    spn_path_t lib;
    spn_path_t bin;
    spn_path_t vendor;
  } paths;

};

struct spn_toolchain_unit_t {
  spn_toolchain_info_t* info;
  spn_cc_t cc;
  sp_hash_t identity;
  sp_hash_t generation;
  sp_str_t version;
};

static inline spn_user_node_t* spn_node_deref(spn_node_ref_t ref) {
  SP_ASSERT(ref.index < sp_da_size(ref.pkg->user_nodes));
  return &ref.pkg->user_nodes[ref.index];
}

#endif
