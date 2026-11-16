#include "git.h"

#include "contend.h"
#include "paths/paths.h"

typedef struct {
  spn_git_cache_t* cache;
  spn_git_checkout_t* checkout;
  sp_atomic_s32_t* cancelled;
  spn_err_t err;
  sp_atomic_s32_t done;
} job_t;

static s32 run(void* user_data) {
  job_t* job = (job_t*)user_data;
  job->err = spn_git_checkout_ensure(job->cache, job->checkout, job->cancelled);
  sp_atomic_s32_store(&job->done, 1, SP_ATOMIC_SEQ_CST);
  return 0;
}

sp_test(git_cache, init_waits_for_the_lock, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t remote = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("A"));
  git_repo_init(remote);
  git_repo_commit(remote, sp_str_lit("A"));

  spn_path_t root = { .sub = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("B")) };
  spn_path_t dbs = spn_path_join(mem, root, sp_str_lit("db"));
  spn_path_roots_t roots = sp_zero;
  spn_git_cache_t cache = sp_zero;
  spn_git_cache_init(&cache, mem, SP_NULLPTR, &roots, dbs, spn_path_join(mem, root, sp_str_lit("checkouts")));
  sp_str_t db = spn_path_str(&roots, mem, spn_path_join(mem, dbs, spn_git_db_key(mem, remote)));
  sp_atomic_s32_t cancelled = sp_zero;
  bool fetching = false;

  spn_git_checkout_id_t id = { .url = remote, .rev = git_repo_head(remote) };
  job_t job = { .cache = &cache, .checkout = spn_git_cache_checkout(&cache, id, &fetching), .cancelled = &cancelled };
  test_contention_t contention = sp_zero;
  sp_try(test_contend_begin(t, &contention, sp_fmt(mem, "{}.lock", sp_fmt_str(db)).value, run, &job, &job.done, true));
  sp_sys_file_meta_t meta = sp_zero;
  sp_expect_err_eq(t, sp_fs_get_link_metadata(db, &meta), SP_ERR_SYS_NOT_FOUND);
  test_contend_end(&contention);

  sp_test_kv_c(t, "message", job.checkout->failure.message);
  sp_expect_eq(t, (u32)job.err, (u32)SPN_OK);
  return SP_OK;
}
