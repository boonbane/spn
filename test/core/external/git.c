#include "spn_test.h"

#include "event/event.h"
#include "external/git.h"
#include "git/remote.h"

typedef enum {
  OP_FETCH,
  OP_CLONE,
} op_t;

typedef enum {
  DAMAGE_NONE,
  DAMAGE_REMOTE_REMOVED,
  DAMAGE_REMOTE_ON_CLOSED_PORT,
  DAMAGE_REPO_REMOVED,
  DAMAGE_ORIGIN_REMOVED,
  DAMAGE_REF_LOCKED,
  DAMAGE_REF_GARBLED,
} damage_t;

typedef struct {
  spn_err_t err;
  spn_git_failure_kind_t kind;
  bool remote;
  u32 retries;
} expect_t;

typedef struct {
  const c8* name;
  op_t op;
  damage_t damage;
  spn_git_transport_t transport;
  bool cancelled;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "fetch_succeeds",
  },
  {
    .name = "clone_of_missing_path_fails",
    .op = OP_CLONE,
    .damage = DAMAGE_REMOTE_REMOVED,
    .expect = { .err = SPN_ERROR },
  },
  {
    .name = "fetch_from_missing_path_fails",
    .damage = DAMAGE_REMOTE_REMOVED,
    .expect = { .err = SPN_ERROR },
  },
  {
    .name = "fetch_outside_a_repo_is_corrupt",
    .damage = DAMAGE_REPO_REMOVED,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CORRUPT },
  },
  {
    .name = "fetch_without_origin_is_corrupt",
    .damage = DAMAGE_ORIGIN_REMOVED,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CORRUPT },
  },
  {
    .name = "fetch_over_garbled_ref_is_corrupt",
    .damage = DAMAGE_REF_GARBLED,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CORRUPT },
  },
  {
    .name = "fetch_into_locked_ref_is_dropped",
    .damage = DAMAGE_REF_LOCKED,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_DROPPED, .remote = true, .retries = 3 },
  },
  {
    .name = "clone_from_closed_port_is_dropped",
    .op = OP_CLONE,
    .damage = DAMAGE_REMOTE_ON_CLOSED_PORT,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_DROPPED, .remote = true, .retries = 3 },
  },
  {
    .name = "system_clone_of_missing_path_is_remote",
    .op = OP_CLONE,
    .damage = DAMAGE_REMOTE_REMOVED,
    .transport = SPN_GIT_TRANSPORT_SYSTEM,
    .expect = { .err = SPN_ERROR, .remote = true },
  },
  {
    .name = "clone_after_cancel_is_cancelled",
    .op = OP_CLONE,
    .cancelled = true,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CANCELLED },
  },
  {
    .name = "fetch_after_cancel_is_not_attempted",
    .cancelled = true,
    .expect = { .err = SPN_ERROR, .kind = SPN_GIT_CANCELLED },
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
    sp_must_eq(t, (u32)spn_git_builtin_clone(remote, local, &cancelled, &failure), (u32)SPN_OK);
  }

  sp_str_t url = remote;
  switch (it->damage) {
    case DAMAGE_NONE: {
      break;
    }
    case DAMAGE_REMOTE_REMOVED: {
      sp_must_ok(t, sp_fs_remove_dir(remote));
      break;
    }
    case DAMAGE_REMOTE_ON_CLOSED_PORT: {
      sp_sys_socket_t probe = SP_SYS_INVALID_SOCKET;
      sp_sys_ipv4_t address = { .octets = { 127, 0, 0, 1 } };
      sp_must_ok(t, sp_sys_socket_open(&probe, (sp_sys_handle_desc_t) { .mode = SP_SYS_BLOCKING }));
      sp_must_ok(t, sp_sys_socket_bind(probe, address));
      sp_must_ok(t, sp_sys_socket_local_port(probe, &address.port));
      sp_must_ok(t, sp_sys_socket_close(probe));
      url = sp_fmt(mem, "git://127.0.0.1:{}/A", sp_fmt_uint(address.port)).value;
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
      sp_must_ok(t, sp_fs_write(sp_fmt(mem, "{}.lock", sp_fmt_str(ref)).value, sp_str_lit(""), SP_FS_WRITE_TRUNCATE));
      break;
    }
    case DAMAGE_REF_GARBLED: {
      sp_must_ok(t, sp_fs_write(ref, sp_str_lit("A"), SP_FS_WRITE_TRUNCATE));
      break;
    }
  }

  failure = (spn_git_failure_t) { .kind = SPN_GIT_DROPPED, .message = "A" };
  sp_atomic_s32_store(&cancelled, it->cancelled, SP_ATOMIC_SEQ_CST);
  spn_net_backoff_t backoff = { .wait_ms = { 1, 1, 1 } };
  spn_err_t err = SPN_OK;
  switch (it->op) {
    case OP_FETCH: {
      err = spn_git_fetch(it->transport, local, url, backoff, &cancelled, &failure);
      break;
    }
    case OP_CLONE: {
      err = spn_git_clone(it->transport, url, local, backoff, &cancelled, &failure);
      break;
    }
  }

  u32 retries = 0;
  sp_da(spn_event_t) events = spn_event_buffer_drain(mem, spn.events);
  sp_da_for(events, at) {
    if (events[at].kind == SPN_EVENT_SYNC_RETRY) {
      retries++;
    }
  }

  sp_test_kv_c(t, "message", failure.message);
  sp_expect_eq(t, (u32)err, (u32)it->expect.err);
  sp_expect_eq(t, (u32)failure.kind, (u32)it->expect.kind);
  sp_expect_eq(t, failure.remote, it->expect.remote);
  sp_expect_eq(t, failure.message[0] != 0, it->expect.err != SPN_OK);
  sp_expect_eq(t, retries, it->expect.retries);
  return SP_OK;
}

sp_test(git, init_bare_over_file_fails, .setup = spn_test_ctx_setup) {
  sp_str_t path = sp_fs_join_path(sp_test_arena(t), test_dir_str(t), sp_str_lit("A"));
  sp_must_ok(t, sp_fs_write(path, sp_str_lit("A"), SP_FS_WRITE_TRUNCATE));

  spn_git_failure_t failure = { .kind = SPN_GIT_DROPPED, .message = "A" };
  spn_err_t err = spn_git_init_bare(sp_str_lit("B"), path, &failure);

  sp_test_kv_c(t, "message", failure.message);
  sp_expect_eq(t, (u32)err, (u32)SPN_ERROR);
  sp_expect_eq(t, (u32)failure.kind, (u32)SPN_GIT_FAILED);
  sp_expect(t, !failure.remote);
  sp_expect(t, failure.message[0] != 0);
  return SP_OK;
}
