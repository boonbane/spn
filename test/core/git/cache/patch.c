#include "../git.h"

#include "paths/paths.h"

typedef struct {
  const c8* commit;
  const c8* dir;
  git_edit_t patches [2];
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
    .name = "patches_apply_in_order",
    .repo = {
      .commits = {
        { .message = "A", .files = { { "F", "A" } } },
      },
    },
    .requests = {
      {
        .commit = "A",
        .patches = {
          { .file = "F", .from = "A", .to = "B" },
          { .file = "F", .from = "B", .to = "C" },
        },
      },
    },
    .expect = {
      { .file = "F", .content = "C" },
    },
  },
  {
    .name = "patches_apply_at_repo_root_before_subdir",
    .repo = {
      .commits = {
        { .message = "A", .files = { { "D/F", "A" } } },
      },
    },
    .requests = {
      {
        .commit = "A",
        .dir = "D",
        .patches = {
          { .file = "D/F", .from = "A", .to = "B" },
        },
      },
    },
    .expect = {
      { .file = "F", .content = "B" },
    },
  },
  {
    .name = "patched_checkout_coexists_with_pristine",
    .repo = {
      .commits = {
        { .message = "A", .files = { { "F", "A" } } },
      },
    },
    .requests = {
      { .commit = "A" },
      {
        .commit = "A",
        .patches = {
          { .file = "F", .from = "A", .to = "B" },
        },
      },
    },
    .expect = {
      { .file = "F", .content = "A" },
      { .file = "F", .content = "B" },
    },
  },
};

sp_test_each(git_cache, patch, test_t, tests, .setup = spn_test_ctx_setup) {
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
  sp_for(at, requests) {
    spn_git_checkout_id_t id = { .url = remote.path, .dir = sp_cstr_as_str(it->requests[at].dir) };
    sp_for(commit, remote.commit_count) {
      if (sp_cstr_equal(it->repo.commits[commit].message, it->requests[at].commit)) {
        id.rev = remote.commits[commit];
      }
    }

    u32 patches = 0;
    sp_carr_detect_len(it->requests[at].patches, patches, it->requests[at].patches[patches].file);
    sp_for(patch, patches) {
      sp_str_t path = sp_fs_join_path(mem, test_dir_str(t), sp_fmt(mem, "{}.{}.patch", sp_fmt_uint(at), sp_fmt_uint(patch)).value);
      sp_try(git_edit_write(t, it->requests[at].patches[patch], path));
      si_da_push(mem, id.patches.files, path);
    }
    u32 missing = 0;
    id.patches.hash = spn_git_patch_hash(id.patches.files, &missing);

    bool fetching = false;
    spn_git_checkout_t* checkout = spn_git_cache_checkout(&cache, id, &fetching);
    spn_err_t err = spn_git_checkout_ensure(&cache, checkout, &cancelled);
    sp_test_kv_c(t, "message", checkout->failure.message);
    sp_must_eq(t, (u32)err, (u32)SPN_OK);
    checkouts[at] = checkout->path;
  }

  sp_for(at, requests) {
    sp_str_t content = sp_zero;
    sp_must_ok(t, sp_fs_read_at(mem, spn_path_at(&roots, spn_path_join(mem, checkouts[at], sp_cstr_as_str(it->expect[at].file))), &content));
    sp_expect_str_eq_c(t, content, it->expect[at].content);
  }
  return SP_OK;
}
