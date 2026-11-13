#include "spn_test.h"

#include "external/git.h"

typedef enum {
  OP_NONE,
  OP_CLONE,
  OP_FETCH,
  OP_INIT_BARE,
} op_t;

typedef enum {
  DAMAGE_NONE,
  DAMAGE_REMOTE_REMOVED,
  DAMAGE_REPO_REMOVED,
  DAMAGE_ORIGIN_REMOVED,
  DAMAGE_REF_LOCKED,
  DAMAGE_REF_GARBLED,
  DAMAGE_PATH_IS_FILE,
} damage_t;

typedef struct {
  spn_err_t err;
  spn_git_failure_kind_t kind;
} expect_t;

typedef struct {
  const c8* name;
  op_t op;
  damage_t damage;
  bool cancelled;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "fetch_succeeds",
    .op = OP_FETCH,
  },
  {
    .name = "clone_of_missing_path_fails",
    .op = OP_CLONE,
    .damage = DAMAGE_REMOTE_REMOVED,
    .expect = { .err = SPN_ERROR },
  },
  {
    .name = "fetch_from_missing_path_fails",
    .op = OP_FETCH,
    .damage = DAMAGE_REMOTE_REMOVED,
    .expect = { .err = SPN_ERROR },
  },
  {
    .name = "fetch_outside_a_repo_is_corrupt",
    .op = OP_FETCH,
    .damage = DAMAGE_REPO_REMOVED,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CORRUPT },
  },
  {
    .name = "fetch_without_origin_is_corrupt",
    .op = OP_FETCH,
    .damage = DAMAGE_ORIGIN_REMOVED,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CORRUPT },
  },
  {
    .name = "fetch_over_garbled_ref_is_corrupt",
    .op = OP_FETCH,
    .damage = DAMAGE_REF_GARBLED,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CORRUPT },
  },
  {
    .name = "fetch_into_locked_ref_is_dropped",
    .op = OP_FETCH,
    .damage = DAMAGE_REF_LOCKED,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_DROPPED },
  },
  {
    .name = "clone_after_cancel_is_cancelled",
    .op = OP_CLONE,
    .cancelled = true,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CANCELLED },
  },
  {
    .name = "fetch_after_cancel_is_not_attempted",
    .op = OP_FETCH,
    .cancelled = true,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CANCELLED },
  },
  {
    .name = "fetch_outside_a_repo_after_cancel_is_cancelled",
    .op = OP_FETCH,
    .damage = DAMAGE_REPO_REMOVED,
    .cancelled = true,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CANCELLED },
  },
  {
    .name = "init_bare_over_file_fails",
    .op = OP_INIT_BARE,
    .damage = DAMAGE_PATH_IS_FILE,
    .expect = { .err = SPN_ERROR },
  },
};

sp_test_each(git, failure, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t remote = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("A"));
  sp_str_t local = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("B"));
  git_repo_init(remote);
  git_repo_commit(remote, sp_str_lit("A"));

  spn_git_failure_t failure = sp_zero;
  sp_atomic_s32_t cancelled = sp_zero;
  sp_str_t branch = sp_zero;
  sp_must_eq(t, (u32)spn_git_current_branch(mem, remote, &branch), (u32)SPN_OK);
  sp_str_t ref = sp_fs_join_path(mem, local, sp_fmt(mem, ".git/refs/remotes/origin/{}", sp_fmt_str(branch)).value);
  if (it->op == OP_FETCH) {
    sp_must_eq(t, (u32)spn_git_clone(remote, local, &cancelled, &failure), (u32)SPN_OK);
  }

  switch (it->damage) {
    case DAMAGE_NONE: {
      break;
    }
    case DAMAGE_REMOTE_REMOVED: {
      sp_must_ok(t, sp_fs_remove_dir(remote));
      break;
    }
    case DAMAGE_REPO_REMOVED: {
      sp_must_ok(t, sp_fs_remove_dir(sp_fs_join_path(mem, local, sp_str_lit(".git"))));
      break;
    }
    case DAMAGE_ORIGIN_REMOVED: {
      git(local, "remote", "remove", "origin");
      break;
    }
    case DAMAGE_REF_LOCKED: {
      git_repo_commit(remote, sp_str_lit("B"));
      sp_must_ok(t, sp_fs_create_file_str(sp_fmt(mem, "{}.lock", sp_fmt_str(ref)).value, sp_str_lit("")));
      break;
    }
    case DAMAGE_REF_GARBLED: {
      sp_must_ok(t, sp_fs_create_file_str(ref, sp_str_lit("A")));
      break;
    }
    case DAMAGE_PATH_IS_FILE: {
      sp_must_ok(t, sp_fs_create_file_str(local, sp_str_lit("A")));
      break;
    }
  }

  failure = (spn_git_failure_t) { .kind = SPN_GIT_DROPPED, .message = "A" };
  sp_atomic_s32_store(&cancelled, it->cancelled, SP_ATOMIC_SEQ_CST);
  spn_err_t err = SPN_OK;
  switch (it->op) {
    case OP_CLONE: {
      err = spn_git_clone(remote, local, &cancelled, &failure);
      break;
    }
    case OP_FETCH: {
      err = spn_git_fetch(local, &cancelled, &failure);
      break;
    }
    case OP_INIT_BARE: {
      err = spn_git_init_bare(remote, local, &failure);
      break;
    }
    case OP_NONE: {
      break;
    }
  }

  sp_test_kv_c(t, "message", failure.message);
  sp_expect_eq(t, (u32)err, (u32)it->expect.err);
  sp_expect_eq(t, (u32)failure.kind, (u32)it->expect.kind);
  sp_expect_eq(t, failure.message[0] != 0, it->expect.err != SPN_OK);
  return SP_OK;
}
