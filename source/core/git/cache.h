#ifndef SPN_GIT_CACHE_H
#define SPN_GIT_CACHE_H

#include "git/types.h"
#include "sp.h"
#include "spn/core.h"

void                spn_git_cache_init(spn_git_cache_t* cache, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_path_t db, spn_path_t checkouts);
spn_err_t           spn_git_cache_ensure_db(spn_git_cache_t* cache, sp_str_t url, sp_atomic_s32_t* cancelled, spn_git_db_t** db);
spn_git_checkout_t* spn_git_cache_checkout(spn_git_cache_t* cache, spn_git_checkout_id_t id, bool* fetching);
spn_err_t           spn_git_checkout_ensure(spn_git_cache_t* cache, spn_git_checkout_t* checkout, sp_atomic_s32_t* cancelled);
spn_err_t           spn_git_db_ensure_rev(spn_git_cache_t* cache, spn_git_db_t* db, sp_str_t rev, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure);

#endif
