#include "spn_test.h"

#include "git/cache.h"
#include "git/remote.h"
#include "index/index.h"
#include "intern/intern.h"
#include "lock/lock.h"
#include "model/sync.h"
#include "paths/paths.h"

typedef struct {
  spn_err_t err;
  const c8* commit;
  bool recorded;
} expect_t;

typedef struct {
  const c8* name;
  const c8* rev;
  const c8* held;
  bool gone;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "unlocked_index_is_placed_at_the_tip",
    .expect = { .commit = "B", .recorded = true },
  },
  {
    .name = "held_pin_stays_behind_the_tip",
    .held = "A",
    .expect = { .commit = "A", .recorded = true },
  },
  {
    .name = "declared_rev_is_not_recorded",
    .rev = "A",
    .expect = { .commit = "A" },
  },
  {
    .name = "missing_remote_fails",
    .gone = true,
    .expect = { .err = SPN_ERROR },
  },
};

static sp_str_t commit_of(const git_repo_fixture_t* fixture, const git_repo_result_t* repo, const c8* message) {
  sp_for(at, repo->commit_count) {
    if (message && sp_cstr_equal(fixture->commits[at].message, message)) {
      return repo->commits[at];
    }
  }
  return sp_str_lit("");
}

sp_test_each(index_pins, sync, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  git_repo_fixture_t fixture = {
    .commits = {
      { .message = "A", .files = { { "F", "A" } } },
      { .message = "B", .files = { { "F", "B" } } },
    },
  };
  git_repo_result_t remote = git_repo_build_at(test_dir_str(t), "R", &fixture);

  spn_ctx_t ctx = { .mem = mem, .heap = mem, .intern = spn.intern, .events = spn.events };
  spn_path_t storage = { .sub = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("S")) };
  spn_git_cache_init(&ctx.caches.git, mem, spn.intern, &ctx.roots, spn_path_join(mem, storage, sp_str_lit("db")), spn_path_join(mem, storage, sp_str_lit("checkouts")), spn_path_join(mem, storage, sp_str_lit("locks")));

  sp_intern_str_t name = spn_intern_lit("I");
  ctx.indexes = sp_da_new(mem, spn_index_info_t);
  sp_da_push(ctx.indexes, ((spn_index_info_t) {
    .id = name.id,
    .name = name,
    .protocol = SPN_INDEX_PROTOCOL_GIT,
    .format = SPN_INDEX_FORMAT_MANIFESTS,
    .git = { .url = remote.path, .rev = commit_of(&fixture, &remote, it->rev) },
    .remote = si_git_remote(SPN_GIT_TRANSPORT_BUILTIN),
  }));
  spn_index_info_t* index = &ctx.indexes[0];

  spn_lock_t kept = spn_lock_new(mem);
  if (it->held) {
    spn_lock_pin_index(&kept, (spn_lock_index_t) { .name = name, .url = remote.path, .rev = commit_of(&fixture, &remote, it->held) });
  }
  if (it->gone) {
    sp_must_ok(t, sp_fs_remove_dir(remote.path));
  }

  spn_lock_t lock = spn_lock_new(mem);
  sp_atomic_s32_t cancelled = sp_zero;
  spn_err_t err = si_sync_index_pins(&ctx, &cancelled, &kept, &lock);

  sp_must_eq(t, (u32)it->expect.err, (u32)err);
  if (err) {
    return SP_OK;
  }

  sp_str_t commit = commit_of(&fixture, &remote, it->expect.commit);
  sp_expect_str_eq(t, index->location.rev, commit);
  sp_expect_str_eq_c(t, test_read_file(mem, sp_fs_join_path(mem, index->location.dir, sp_str_lit("F"))), it->expect.commit);

  sp_must_eq(t, (u32)it->expect.recorded, (u32)sp_da_size(lock.indexes));
  if (it->expect.recorded) {
    sp_expect_str_eq(t, lock.indexes[0].url, remote.path);
    sp_expect_str_eq(t, lock.indexes[0].rev, commit);
  }
  return SP_OK;
}
