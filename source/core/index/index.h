#ifndef SPN_INDEX_H
#define SPN_INDEX_H

#include "sp.h"
#include "spn/core.h"
#include "index/types.h"
#include "spn/errors.h"
#include "external/git.h"

#define SPN_INDEX_DEFAULT_REFRESH 600

typedef struct {
  bool present;
  bool fetch;
  bool held;
  bool force;
} spn_index_plan_t;

spn_index_plan_t spn_index_plan(spn_index_info_t* index, bool force);
spn_err_t        spn_index_sync(spn_index_info_t* index, spn_index_plan_t plan, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure);
spn_index_location_t spn_index_location(spn_index_info_t* index, sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t store);
void             spn_index_locate(spn_index_info_t* index, sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t store);
bool             spn_index_is_snapshot(const spn_index_info_t* index);
spn_err_t        spn_index_tip(spn_index_info_t* index, sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t store, sp_atomic_s32_t* cancelled, sp_str_t* rev, spn_git_failure_t* failure);
spn_index_info_t* spn_index_find(sp_da(spn_index_info_t)* indexes, spn_index_id_t id);
void             spn_index_assemble(sp_mem_t mem, spn_index_map_t* workspace, sp_da(spn_index_info_t) user, sp_da(spn_index_info_t)* indexes);
spn_err_t        spn_index_get_package(spn_index_info_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err);
spn_err_t        spn_index_publish(spn_index_info_t* index, spn_index_release_t* rel, sp_atomic_s32_t* cancelled);
sp_str_t         spn_index_source(spn_index_info_t* index);
sp_str_t         spn_index_publish_target(spn_index_info_t* index);

#endif
