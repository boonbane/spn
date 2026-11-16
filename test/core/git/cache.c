#include "git.h"

#include "paths/paths.h"

typedef struct {
  const c8* commit;
  const c8* dir;
} request_t;

typedef struct {
  const c8* file;
  const c8* content;
} expect_t;

typedef struct {
  const c8* name;
  git_repo_fixture_t repo;
  request_t requests [2];
  expect_t expect [2];
} test_t;

static const test_t tests [] = {
  {
    .name = "each_commit_is_its_own_checkout",
    .repo = {
      .commits = {
        { .message = "A", .files = { { "F", "A" } } },
        { .message = "B", .files = { { "F", "B" } } },
      },
    },
    .requests = {
      { .commit = "A" },
      { .commit = "B" },
    },
    .expect = {
      { .file = "F", .content = "A" },
      { .file = "F", .content = "B" },
    },
  },
  {
    .name = "subdirectory_is_the_checkout",
    .repo = {
      .commits = {
        { .message = "A", .files = { { "D/F", "A" } } },
      },
    },
    .requests = {
      { .commit = "A", .dir = "D" },
    },
    .expect = {
      { .file = "F", .content = "A" },
    },
  },
  {
    .name = "same_request_is_the_same_checkout",
    .repo = {
      .commits = {
        { .message = "A", .files = { { "D/F", "A" } } },
      },
    },
    .requests = {
      { .commit = "A", .dir = "D" },
      { .commit = "A", .dir = "D" },
    },
    .expect = {
      { .file = "F", .content = "A" },
    },
  },
};

sp_test_each(git_cache, checkout, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  git_repo_result_t remote = git_repo_build_at(test_dir_str(t), "A", &it->repo);

  spn_path_t root = { .sub = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("B")) };
  spn_path_roots_t roots = sp_zero;
  spn_git_cache_t cache = sp_zero;
  spn_git_cache_init(&cache, mem, SP_NULLPTR, &roots, spn_path_join(mem, root, sp_str_lit("db")), spn_path_join(mem, root, sp_str_lit("checkouts")));
  sp_atomic_s32_t cancelled = sp_zero;

  u32 requests = 0;
  sp_carr_detect_len(it->requests, requests, it->requests[requests].commit);
  spn_path_t checkouts [sp_carr_len(it->requests)] = sp_zero;
  u32 distinct = 0;
  sp_for(at, requests) {
    spn_git_checkout_id_t id = { .url = remote.path, .dir = sp_cstr_as_str(it->requests[at].dir) };
    sp_for(commit, remote.commit_count) {
      if (sp_cstr_equal(it->repo.commits[commit].message, it->requests[at].commit)) {
        id.rev = remote.commits[commit];
      }
    }

    bool fetching = false;
    spn_git_checkout_t* checkout = spn_git_cache_checkout(&cache, id, &fetching);
    spn_err_t err = spn_git_checkout_ensure(&cache, checkout, &cancelled);
    sp_test_kv_c(t, "message", checkout->failure.message);
    sp_must_eq(t, (u32)err, (u32)SPN_OK);

    u32 known = 0;
    while (known < distinct && !spn_path_equal(checkouts[known], checkout->path)) {
      known++;
    }
    bool fresh = known == distinct;
    sp_expect_eq(t, fetching, fresh);
    if (fresh) {
      checkouts[distinct++] = checkout->path;
    }
  }

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].file);
  sp_must_eq(t, distinct, expected);
  sp_for(at, expected) {
    sp_str_t content = sp_zero;
    sp_must_ok(t, sp_fs_read_at(mem, spn_path_at(&roots, spn_path_join(mem, checkouts[at], sp_cstr_as_str(it->expect[at].file))), &content));
    sp_expect_str_eq_c(t, content, it->expect[at].content);
  }
  return SP_OK;
}
