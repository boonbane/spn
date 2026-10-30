#ifndef SPN_SESSION_TYPES_H
#define SPN_SESSION_TYPES_H

#include "sp.h"

#include "spn/core.h"
#include "spn/types.h"

#include "array/array.h"
#include "compiler/types.h"
#include "core/types.h"
#include "dag/types.h"
#include "git/types.h"
#include "options/types.h"
#include "profile/types.h"
#include "resolve/types.h"
#include "session/registry/types.h"
#include "target/types.h"
#include "toolchain/types.h"
#include "unit/types.h"

typedef struct {
  sp_intern_str_t qualified;
  spn_git_patch_set_t set;
} spn_patch_t;

struct spn_session_t {
  spn_ctx_t* ctx;
  spn_project_t* project;
  sp_mem_arena_t* arena;
  sp_mem_t mem;
  const si_pkg_info_t* manifest;

  bool force;

  spn_resolve_t resolve;
  si_da(spn_patch_t) patches;
  spn_pkg_registry_t registry;

  struct {
    spn_option_seeds_t seeds;
    u32 resolves;
  } gates;

  struct {
    u32 kinds;
    si_om(sp_intern_id_t, sp_intern_str_t) names;
  } selection;
  spn_build_plan_t plan;

  struct {
    spn_build_unit_t* program;
    spn_build_unit_t* metaprogram;
    si_da(spn_toolchain_unit_t*) toolchains;
  } builds;
  spn_units_t units;

  struct {
    spn_path_t root;
    spn_path_t build;
  } paths;

  struct {
    spn_dag_file_cache_t files;
    spn_path_t files_path;
    spn_dag_store_t store;
    spn_dag_action_cache_t actions;
    spn_dag_obs_table_t discovery;
    spn_path_t scratch;
    spn_dag_stats_t stats;
  } dag;
};

#endif
