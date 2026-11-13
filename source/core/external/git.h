#ifndef SPN_EXTERNAL_GIT_H
#define SPN_EXTERNAL_GIT_H

#include "sp.h"
#include "spn/core.h"
#include "external/net.h"

#define spn_git_message_max 512

typedef enum {
  SPN_GIT_FAILED,
  SPN_GIT_DROPPED,
  SPN_GIT_UNTRUSTED,
  SPN_GIT_CORRUPT,
  SPN_GIT_CANCELLED,
} spn_git_failure_kind_t;

typedef struct {
  spn_git_failure_kind_t kind;
  c8 message [spn_git_message_max];
} spn_git_failure_t;

spn_err_t spn_git_init(sp_str_t ca_bundle, u32 timeout_s);
spn_err_t spn_git_clone(sp_str_t url, sp_str_t path, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure);
spn_err_t spn_git_init_bare(sp_str_t url, sp_str_t path, spn_git_failure_t* failure);
spn_err_t spn_git_clone_shared(sp_mem_t mem, sp_str_t source, sp_str_t path, sp_str_t* error);
spn_err_t spn_git_fetch(sp_str_t repo, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure);
spn_err_t spn_git_fetch_rev(sp_str_t repo, sp_str_t rev, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure);
bool spn_git_has_rev(sp_str_t repo, sp_str_t rev);
spn_err_t spn_git_checkout(sp_str_t repo, sp_str_t commit);
spn_err_t spn_git_apply(sp_mem_t mem, sp_str_t repo, sp_str_t patch, sp_str_t* error);
spn_err_t spn_git_get_remote_url(sp_mem_t mem, sp_str_t repo_path, sp_str_t* url);
spn_err_t spn_git_get_commit_full(sp_mem_t mem, sp_str_t repo_path, sp_str_t id, sp_str_t* sha);
spn_err_t spn_git_get_root(sp_mem_t mem, sp_str_t cwd, sp_str_t* root);
spn_err_t spn_git_default_branch(sp_mem_t mem, sp_str_t repo, sp_str_t* branch);
spn_err_t spn_git_current_branch(sp_mem_t mem, sp_str_t repo, sp_str_t* branch);
bool spn_git_has_remote_branches(sp_str_t repo);
spn_err_t spn_git_checkout_branch(sp_str_t repo, sp_str_t branch);
spn_err_t spn_git_clean(sp_str_t repo);
spn_err_t spn_git_add(sp_str_t repo, sp_str_t path);
spn_err_t spn_git_commit(sp_str_t repo, sp_str_t message);
spn_err_t spn_git_push(sp_mem_t mem, sp_str_t repo, sp_str_t url, sp_str_t refspec, sp_str_t* output);
bool spn_git_is_dirty(sp_str_t repo, sp_str_t dir);
bool spn_git_rev_on_remote(sp_str_t repo, sp_str_t rev);

#endif
