#include "spn_test.h"

#include "git/cache.h"
#include "git/remote.h"
#include "index/index.h"
#include "intern/intern.h"
#include "lock/lock.h"
#include "model/sync.h"
#include "paths/paths.h"
#include "semver/compare.h"

#define PINS_TEST_MAX_COMMITS 2

typedef struct {
  spn_err_t err;
  spn_semver_t version;
  bool recorded;
} expect_t;

typedef struct {
  const c8* name;
  const c8* commits [PINS_TEST_MAX_COMMITS];
  const c8* rev;
  const c8* held;
  bool gone;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "unlocked_index_is_placed_at_the_tip",
    .commits = { "older", "newer" },
    .expect = { .version = { 2, 0, 0 }, .recorded = true },
  },
  {
    .name = "held_pin_stays_behind_the_tip",
    .commits = { "older", "newer" },
    .held = "older",
    .expect = { .version = { 1, 0, 0 }, .recorded = true },
  },
  {
    .name = "declared_rev_is_not_recorded",
    .commits = { "older", "newer" },
    .rev = "older",
    .expect = { .version = { 1, 0, 0 } },
  },
  {
    .name = "missing_remote_fails",
    .commits = { "older" },
    .gone = true,
    .expect = { .err = SPN_ERROR },
  },
};

static sp_str_t commit_of(const test_t* test, const sp_str_t* revs, const c8* fixture) {
  sp_carr_for(test->commits, at) {
    if (fixture && test->commits[at] && sp_cstr_equal(test->commits[at], fixture)) {
      return revs[at];
    }
  }
  return sp_str_lit("");
}

sp_test_each(index_pins, sync, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t remote = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("R"));
  sp_str_t package = sp_fs_join_path(mem, remote, sp_str_lit("A"));
  git_repo_init(remote);
  sp_fs_create_dir(package);

  sp_str_t revs [PINS_TEST_MAX_COMMITS] = sp_zero;
  u32 commits = 0;
  sp_carr_detect_len(it->commits, commits, it->commits[commits]);
  sp_for(at, commits) {
    sp_str_t fixture = test_repo_path(mem, sp_test_format(t, "test/core/index/packages/{}.toml", sp_fmt_cstr(it->commits[at])));
    sp_str_t toml = sp_zero;
    sp_must_ok(t, sp_fs_read(mem, fixture, &toml));
    sp_must_ok(t, sp_fs_write(sp_fs_join_path(mem, package, sp_str_lit("spn.toml")), toml, SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms));
    git_repo_stage_all(remote);
    git_repo_commit(remote, sp_cstr_as_str(it->commits[at]));
    revs[at] = git_repo_head(remote);
  }

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
    .git = { .url = remote, .rev = commit_of(it, revs, it->rev) },
    .remote = spn_git_remote(SPN_GIT_TRANSPORT_BUILTIN),
  }));
  spn_index_info_t* index = &ctx.indexes[0];

  spn_lock_t kept = spn_lock_new(mem);
  if (it->held) {
    spn_lock_pin_index(&kept, (spn_lock_index_t) { .name = name, .url = remote, .rev = commit_of(it, revs, it->held) });
  }
  if (it->gone) {
    sp_must_ok(t, sp_fs_remove_dir(remote));
  }

  spn_lock_t lock = spn_lock_new(mem);
  sp_atomic_s32_t cancelled = sp_zero;
  spn_err_t err = si_sync_index_pins(&ctx, &cancelled, &kept, &lock);

  sp_must_eq(t, (u32)it->expect.err, (u32)err);
  if (err) {
    return SP_OK;
  }

  spn_index_pkg_t* pkg = SP_NULLPTR;
  spn_err_union_t diag = sp_zero;
  sp_must_eq(t, (u32)SPN_OK, (u32)spn_index_get_package(index, mem, spn.intern, &ctx.roots, spn_intern_lit("A"), &pkg, &diag));
  sp_must(t, pkg != SP_NULLPTR);
  sp_must_eq(t, 1, (u32)sp_da_size(pkg->releases));
  spn_index_release_t* release = &pkg->releases[0];
  sp_expect(t, spn_semver_eq(it->expect.version, release->version));
  sp_must_eq(t, (u32)SPN_PKG_ROOT_GIT, (u32)release->source.kind);
  sp_expect_str_eq(t, release->source.git.url, remote);
  sp_expect_str_eq(t, release->source.git.rev, index->location.rev);
  sp_expect_str_eq_c(t, release->source.git.dir, "A");

  sp_must_eq(t, (u32)it->expect.recorded, (u32)sp_da_size(lock.indexes));
  if (it->expect.recorded) {
    sp_expect_str_eq(t, lock.indexes[0].url, remote);
    sp_expect_str_eq(t, lock.indexes[0].rev, index->location.rev);
  }
  return SP_OK;
}
