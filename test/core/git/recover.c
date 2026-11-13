#include "git.h"

#include "paths/paths.h"

typedef enum {
  DAMAGE_NONE,
  DAMAGE_ORIGIN_REMOVED,
  DAMAGE_REF_GARBLED,
  DAMAGE_REF_LOCKED,
  DAMAGE_OBJECTS_REMOVED,
} damage_t;

typedef struct {
  spn_err_t err;
  bool rebuilt;
} expect_t;

typedef struct {
  const c8* name;
  bool checked_out;
  damage_t damage;
  bool unreachable;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "db_without_origin_is_rebuilt",
    .damage = DAMAGE_ORIGIN_REMOVED,
    .expect = { .rebuilt = true },
  },
  {
    .name = "db_with_garbled_ref_is_rebuilt",
    .damage = DAMAGE_REF_GARBLED,
    .expect = { .rebuilt = true },
  },
  {
    .name = "db_with_locked_ref_is_kept",
    .damage = DAMAGE_REF_LOCKED,
  },
  {
    .name = "db_is_kept_when_rebuild_fails",
    .damage = DAMAGE_ORIGIN_REMOVED,
    .unreachable = true,
    .expect = { .err = SPN_ERROR },
  },
  {
    .name = "finished_checkout_needs_no_db",
    .checked_out = true,
    .damage = DAMAGE_OBJECTS_REMOVED,
    .unreachable = true,
  },
};

sp_test_each(git_recover, ensure, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t remote = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("A"));
  git_repo_init(remote);
  git_repo_commit(remote, sp_str_lit("A"));

  sp_str_t branch = sp_zero;
  sp_must_eq(t, (u32)spn_git_current_branch(mem, remote, &branch), (u32)SPN_OK);
  spn_git_checkout_id_t id = { .url = remote, .rev = git_repo_head(remote) };

  spn_path_t root = { .sub = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("B")) };
  spn_path_t dbs = spn_path_join(mem, root, sp_str_lit("db"));
  spn_path_t checkouts = spn_path_join(mem, root, sp_str_lit("checkouts"));
  spn_path_roots_t roots = sp_zero;
  bool fetching = false;

  spn_git_cache_t before = sp_zero;
  spn_git_cache_init(&before, mem, SP_NULLPTR, &roots, dbs, checkouts);
  spn_git_db_t* db = SP_NULLPTR;
  sp_must_eq(t, (u32)spn_git_cache_ensure_db(&before, remote, &db), (u32)SPN_OK);
  if (it->checked_out) {
    sp_must_eq(t, (u32)spn_git_checkout_ensure(&before, spn_git_cache_checkout(&before, id, &fetching)), (u32)SPN_OK);
  }

  sp_str_t repo = spn_path_str(&roots, mem, db->path);
  sp_str_t ref = sp_fs_join_path(mem, repo, sp_fmt(mem, "refs/heads/{}", sp_fmt_str(branch)).value);
  sp_str_t witness = sp_fs_join_path(mem, repo, sp_str_lit("A"));
  sp_must_ok(t, sp_fs_create_file_str(witness, sp_str_lit("A")));

  switch (it->damage) {
    case DAMAGE_NONE: {
      break;
    }
    case DAMAGE_ORIGIN_REMOVED: {
      git(repo, "remote", "remove", "origin");
      break;
    }
    case DAMAGE_REF_GARBLED: {
      sp_must_ok(t, sp_fs_create_file_str(ref, sp_str_lit("A")));
      break;
    }
    case DAMAGE_REF_LOCKED: {
      sp_must_ok(t, sp_fs_create_file_str(sp_fmt(mem, "{}.lock", sp_fmt_str(ref)).value, sp_str_lit("")));
      break;
    }
    case DAMAGE_OBJECTS_REMOVED: {
      sp_must_ok(t, sp_fs_remove_dir(sp_fs_join_path(mem, repo, sp_str_lit("objects"))));
      break;
    }
  }
  if (it->unreachable) {
    sp_must_ok(t, sp_fs_remove_dir(remote));
  }

  spn_git_cache_t cache = sp_zero;
  spn_git_cache_init(&cache, mem, SP_NULLPTR, &roots, dbs, checkouts);
  spn_git_checkout_t* checkout = spn_git_cache_checkout(&cache, id, &fetching);
  spn_err_t err = spn_git_checkout_ensure(&cache, checkout);

  sp_test_kv(t, "error", checkout->error);
  sp_expect_eq(t, (u32)err, (u32)it->expect.err);
  sp_expect_eq(t, !sp_fs_exists(witness), it->expect.rebuilt);

  u32 entries = 0;
  sp_sys_dir_t dir = sp_zero;
  sp_must_ok(t, sp_fs_open_dir_at(spn_path_at(&roots, dbs), &dir));
  SP_ALIGNED u8 buf [SP_SYS_DIR_MIN_BUF];
  sp_fs_dir_it_t walk = sp_fs_dir_it(dir);
  sp_fs_dir_entry_t entry = sp_zero;
  while (sp_fs_dir_it_next(&walk, sp_mem_slice(buf, sizeof(buf)), &entry)) {
    entries++;
  }
  sp_fs_dir_it_close(&walk);
  sp_fs_close_dir(dir);
  sp_expect_eq(t, entries, 1u);
  return SP_OK;
}
