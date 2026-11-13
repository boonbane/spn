#include "index.h"

typedef struct {
  const c8* name;

  spn_index_protocol_t protocol;
  u32 refresh;
  bool cloned;
  bool mutate_remote;
  bool remove_remote;
  bool remove_repo;
  bool cancelled;

  struct {
    spn_err_t result;
    const c8* cached;
    bool cause;
    bool fresh;
  } expect;
} sync_test_t;

static const sync_test_t tests [] = {
  {
    .name = "clone_when_missing",
    .expect = {
      .cached = "A",
    },
  },
  {
    .name = "fetch_when_present",
    .cloned = true,
    .mutate_remote = true,
    .expect = {
      .cached = "B",
    },
  },
  {
    .name = "http_unimplemented",
    .protocol = SPN_INDEX_PROTOCOL_HTTP,
    .expect = {
      .result = SPN_ERROR,
    },
  },
  {
    .name = "failed_clone_reports_cause",
    .remove_remote = true,
    .expect = {
      .result = SPN_ERROR,
      .cause = true,
    },
  },
  {
    .name = "failed_fetch_keeps_cache",
    .cloned = true,
    .remove_remote = true,
    .expect = {
      .result = SPN_ERROR,
      .cached = "A",
      .cause = true,
    },
  },
  {
    .name = "corrupt_repo_is_rebuilt",
    .cloned = true,
    .mutate_remote = true,
    .remove_repo = true,
    .expect = {
      .cached = "B",
    },
  },
  {
    .name = "failed_rebuild_keeps_cache",
    .cloned = true,
    .remove_remote = true,
    .remove_repo = true,
    .expect = {
      .result = SPN_ERROR,
      .cached = "A",
      .cause = true,
    },
  },
  {
    .name = "cancelled_clone_fails",
    .cancelled = true,
    .expect = {
      .result = SPN_ERROR,
      .cause = true,
    },
  },
  {
    .name = "cancelled_fetch_keeps_cache",
    .cloned = true,
    .mutate_remote = true,
    .cancelled = true,
    .expect = {
      .result = SPN_ERROR,
      .cached = "A",
      .cause = true,
    },
  },
  {
    .name = "corrupt_repo_is_kept_when_cancelled",
    .cloned = true,
    .mutate_remote = true,
    .remove_repo = true,
    .cancelled = true,
    .expect = {
      .result = SPN_ERROR,
      .cached = "A",
      .cause = true,
    },
  },
  {
    .name = "fresh_clone_is_not_stale",
    .refresh = 600,
    .expect = {
      .cached = "A",
      .fresh = true,
    },
  },
};

static void commit_seed(sp_mem_t mem, sp_str_t repo, const c8* content) {
  sp_fs_create_file_cstr(sp_fs_join_path(mem, repo, sp_str_lit("seed.txt")), content);

  git_repo_stage_all(repo);
  git_repo_commit(repo, sp_str_lit("seed"));
}

typedef struct {
  const c8* name;
  bool exists;

  struct {
    spn_err_t result;
  } expect;
} dir_sync_test_t;

static const dir_sync_test_t dir_tests [] = {
  {
    .name = "present",
    .exists = true,
  },
  {
    .name = "missing",
    .expect = {
      .result = SPN_ERROR,
    },
  },
};

sp_test_each(index_sync, dir, dir_sync_test_t, dir_tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t location = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("index"));
  if (it->exists) {
    sp_fs_create_dir(location);
  }

  spn_index_info_t index = {
    .location = { .at = sp_path_from_str(location), .dir = location },
    .protocol = SPN_INDEX_PROTOCOL_DIR,
  };

  spn_index_plan_t plan = spn_index_plan(&index, false);
  spn_git_failure_t failure = sp_zero;
  sp_atomic_s32_t cancelled = sp_zero;
  sp_expect_eq(t, it->expect.result, spn_index_sync(&index, plan, &cancelled, &failure));
  if (it->exists) {
    sp_expect_eq(t, false, plan.fetch);
  }

  return SP_OK;
}

sp_test_each(index_sync, sync, sync_test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t tmp = test_dir_str(t);

  sp_str_t remote = sp_fs_join_path(mem, tmp, sp_str_lit("remote/index"));
  sp_str_t cache = sp_fs_join_path(mem, tmp, sp_str_lit("cache/index"));

  sp_fs_create_dir(remote);
  git_repo_init(remote);
  commit_seed(mem, remote, "A");

  spn_index_info_t index = {
    .protocol = it->protocol,
    .git = { .url = remote },
    .location = { .at = sp_path_from_str(cache), .dir = cache },
    .refresh = it->refresh,
  };

  spn_git_failure_t failure = sp_zero;
  sp_atomic_s32_t cancelled = sp_zero;
  if (it->cloned) {
    sp_expect_eq(t, SPN_OK, spn_index_sync(&index, spn_index_plan(&index, false), &cancelled, &failure));
  }
  if (it->mutate_remote) {
    commit_seed(mem, remote, "B");
  }
  if (it->remove_remote) {
    sp_fs_remove_dir(remote);
  }
  if (it->remove_repo) {
    sp_fs_remove_dir(sp_fs_join_path(mem, cache, sp_str_lit(".git")));
  }

  sp_atomic_s32_store(&cancelled, it->cancelled, SP_ATOMIC_SEQ_CST);
  sp_expect_eq(t, it->expect.result, spn_index_sync(&index, spn_index_plan(&index, false), &cancelled, &failure));
  sp_expect_str_eq(t, test_read_file(mem, sp_fs_join_path(mem, cache, sp_str_lit("seed.txt"))), sp_cstr_as_str(it->expect.cached));
  sp_expect_eq(t, it->expect.cause, failure.message[0] != 0);

  spn_index_plan_t plan = spn_index_plan(&index, false);
  sp_expect_eq(t, it->expect.fresh, plan.present && !plan.fetch);

  return SP_OK;
}
