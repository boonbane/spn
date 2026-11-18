#include "../git.h"

#include "git/remote.h"
#include "paths/paths.h"

sp_test(git_cache, system_git_fetches_tip_then_history, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t remote = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("A"));
  git_repo_init(remote);
  git_repo_commit(remote, sp_str_lit("A"));
  sp_str_t abbreviated = sp_str_prefix(git_repo_head(remote), 12);
  git_repo_commit(remote, sp_str_lit("B"));
  sp_str_t tip = sp_str_trim_right(git(remote, "rev-parse", "HEAD").out);

  spn_path_t root = { .sub = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("B")) };
  spn_path_t dbs = spn_path_join(mem, root, sp_str_lit("db"));
  spn_path_roots_t roots = sp_zero;
  spn_git_cache_t cache = sp_zero;
  spn_git_cache_init(&cache, mem, SP_NULLPTR, &roots, dbs, spn_path_join(mem, root, sp_str_lit("checkouts")), spn_path_join(mem, root, sp_str_lit("locks")));
  cache.remote = spn_git_remote(SPN_GIT_TRANSPORT_SYSTEM);
  sp_str_t db = spn_path_str(&roots, mem, spn_path_join(mem, dbs, spn_git_db_key(mem, remote)));
  sp_atomic_s32_t cancelled = sp_zero;
  bool fetching = false;

  struct {
    sp_str_t rev;
    bool shallow;
  } steps [] = {
    { .rev = tip, .shallow = true },
    { .rev = abbreviated },
  };
  sp_carr_for(steps, at) {
    spn_git_checkout_id_t id = { .url = remote, .rev = steps[at].rev };
    spn_git_checkout_t* checkout = spn_git_cache_checkout(&cache, id, &fetching);
    spn_err_t err = spn_git_checkout_ensure(&cache, checkout, &cancelled);
    sp_test_kv_c(t, "message", checkout->failure.message);
    sp_expect_eq(t, (u32)err, (u32)SPN_OK);
    sp_expect_eq(t, spn_git_is_shallow(db), steps[at].shallow);
  }
  return SP_OK;
}
