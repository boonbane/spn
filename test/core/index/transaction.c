#include "index.h"

#include "contend.h"

typedef enum {
  TXN_ACTION_NONE,
  TXN_ACTION_PUBLISH,
  TXN_ACTION_SYNC,
  TXN_ACTION_ADVANCE_REMOTE,
  TXN_ACTION_REMOVE_REMOTE,
  TXN_ACTION_APPEND_CLONE_FILE,
  TXN_ACTION_CREATE_CLONE_FILE,
  TXN_ACTION_DETACH_HEAD,
} txn_action_kind_t;

typedef struct {
  txn_action_kind_t kind;

  union {
    struct { const c8* name; spn_semver_t version; spn_err_t err; bool contended; } publish;
    struct { spn_err_t err; } sync;
    struct { const c8* file; const c8* line; } write;
  };
} txn_action_t;

typedef struct {
  const c8* file;
  u32 lines;
} txn_file_t;

typedef struct {
  const c8* name;

  struct {
    bool empty;
    bool aliased;
    const c8* hook;
    const c8* pin;
  } remote;

  txn_action_t actions [8];

  struct {
    txn_file_t files [2];
    txn_file_t cached [2];
    const c8* head;
    const c8* witness;
  } expect;
} txn_test_t;

static const txn_test_t tests [] = {
  {
    .name = "publish_clones_commits_pushes",
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
    },
    .expect = {
      .files = { { .file = ".spn/index/spum.jsonl", .lines = 1 } },
      .head = "spum 1.0.0",
    },
  },
  {
    .name = "duplicate_rejected_after_freshen",
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 }, .err = SPN_ERR_VERSION_EXISTS } },
    },
    .expect = {
      .files = { { .file = ".spn/index/spum.jsonl", .lines = 1 } },
    },
  },
  {
    .name = "retries_after_remote_advances",
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
      {
        .kind = TXN_ACTION_ADVANCE_REMOTE,
        .write = {
          .file = ".spn/index/other.jsonl",
          .line = "{" kv("name", "other") "," kv("version", "1.0.0") "," kv("yanked", false) "}",
        },
      },
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 1, 0 } } },
    },
    .expect = {
      .files = {
        { .file = ".spn/index/spum.jsonl", .lines = 2 },
        { .file = ".spn/index/other.jsonl", .lines = 1 },
      },
    },
  },
  {
    .name = "discards_tracked_damage",
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
      { .kind = TXN_ACTION_APPEND_CLONE_FILE, .write = { .file = ".spn/index/spum.jsonl", .line = "not json" } },
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 1, 0 } } },
    },
    .expect = {
      .files = { { .file = ".spn/index/spum.jsonl", .lines = 2 } },
    },
  },
  {
    .name = "retries_rejected_push",
    .remote = { .hook = "reject_once" },
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
    },
    .expect = {
      .files = { { .file = ".spn/index/spum.jsonl", .lines = 1 } },
      .head = "spum 1.0.0",
    },
  },
  {
    .name = "push_cannot_prompt",
    .remote = { .hook = "note_unattended" },
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
    },
    .expect = {
      .witness = "unattended",
    },
  },
  {
    .name = "pushes_through_publish_url",
    .remote = { .aliased = true },
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
    },
    .expect = {
      .files = { { .file = ".spn/index/spum.jsonl", .lines = 1 } },
      .head = "spum 1.0.0",
    },
  },
  {
    .name = "release_survives_a_failed_refresh",
    .remote = { .aliased = true },
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
      { .kind = TXN_ACTION_REMOVE_REMOTE },
      { .kind = TXN_ACTION_SYNC, .sync = { .err = SPN_ERROR } },
    },
    .expect = {
      .cached = { { .file = ".spn/index/spum.jsonl", .lines = 1 } },
    },
  },
  {
    .name = "bootstraps_empty_remote",
    .remote = { .empty = true },
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
    },
    .expect = {
      .files = { { .file = ".spn/index/spum.jsonl", .lines = 1 } },
    },
  },
  {
    .name = "retries_rejected_bootstrap_push",
    .remote = { .empty = true, .hook = "reject_once" },
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
    },
    .expect = {
      .files = { { .file = ".spn/index/spum.jsonl", .lines = 1 } },
      .head = "spum 1.0.0",
    },
  },
  {
    .name = "cleans_untracked_damage",
    .actions = {
      { .kind = TXN_ACTION_SYNC },
      { .kind = TXN_ACTION_CREATE_CLONE_FILE, .write = { .file = ".spn/index/spum.jsonl", .line = "not json" } },
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
    },
    .expect = {
      .files = { { .file = ".spn/index/spum.jsonl", .lines = 1 } },
    },
  },
  {
    .name = "pinned_rejected",
    .remote = { .pin = "abc123" },
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 }, .err = SPN_ERR_INDEX_PINNED } },
    },
  },
  {
    .name = "reattaches_detached_head",
    .actions = {
      { .kind = TXN_ACTION_SYNC },
      { .kind = TXN_ACTION_DETACH_HEAD },
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 } } },
    },
    .expect = {
      .head = "spum 1.0.0",
    },
  },
  {
    .name = "waits_for_the_lock",
    .actions = {
      { .kind = TXN_ACTION_PUBLISH, .publish = { .name = "spum", .version = { 1, 0, 0 }, .contended = true } },
    },
    .expect = {
      .head = "spum 1.0.0",
    },
  },
};

typedef struct {
  spn_index_info_t* index;
  spn_index_release_t* rel;
  sp_atomic_s32_t* cancelled;
  spn_err_t err;
  sp_atomic_s32_t done;
} publish_job_t;

static s32 run_publish(void* user_data) {
  publish_job_t* job = (publish_job_t*)user_data;
  job->err = spn_index_publish(job->index, job->rel, job->cancelled);
  sp_atomic_s32_store(&job->done, 1, SP_ATOMIC_SEQ_CST);
  return 0;
}

static u32 remote_line_count(sp_mem_t mem, sp_str_t remote, const c8* path) {
  sp_ps_output_t result = sp_ps_run(mem, (sp_ps_config_t) {
    .command = SP_LIT("git"),
    .args = { SP_LIT("-C"), remote, SP_LIT("show"), sp_fmt(mem, "HEAD:{}", sp_fmt_cstr(path)).value },
  });
  if (result.status.exit_code) {
    return 0;
  }

  return index_test_count_lines(result.out);
}

static sp_str_t remote_head_message(sp_mem_t mem, sp_str_t remote) {
  sp_ps_output_t result = sp_ps_run(mem, (sp_ps_config_t) {
    .command = SP_LIT("git"),
    .args = { SP_LIT("-C"), remote, SP_LIT("log"), SP_LIT("--format=%s"), SP_LIT("-1") },
  });
  return sp_str_trim_right(result.out);
}

sp_test_each(index_transaction, publish, txn_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t tmp = test_dir_str(t);

  sp_str_t seed = sp_fs_join_path(mem, tmp, sp_str_lit("seed"));
  sp_str_t remote = sp_fs_join_path(mem, tmp, sp_str_lit("remote.git"));
  sp_str_t clone = sp_fs_join_path(mem, tmp, sp_str_lit("clone"));

  spn_index_info_t index = {
    .protocol = SPN_INDEX_PROTOCOL_GIT,
    .git = { .url = remote },
    .location = { .at = sp_path_from_str(clone), .dir = clone },
  };
  if (it->remote.pin) {
    index.git.rev = sp_cstr_as_str(it->remote.pin);
  }
  if (it->remote.aliased) {
    index.git.publish_url = sp_fs_join_path(mem, tmp, sp_str_lit("./remote.git"));
  }

  if (it->remote.empty) {
    git(tmp, "init", "--quiet", "--bare", "remote.git");
  }
  else {
    sp_fs_create_dir(seed);
    git_repo_init(seed);
    git(seed, "symbolic-ref", "HEAD", "refs/heads/main");
    sp_fs_write(sp_fs_join_path(mem, seed, sp_str_lit("README.md")), sp_str_lit("index\n"), SP_FS_WRITE_TRUNCATE);
    git_repo_stage_all(seed);
    git_repo_commit(seed, sp_str_lit("init"));
    git(tmp, "clone", "--quiet", "--bare", "seed", "remote.git");
  }

  if (it->remote.hook) {
    sp_must_ok(t, sp_fs_copy_file_at(
      sp_path_cwd(test_repo_path(mem, sp_fs_join_path(mem, sp_str_lit("test/core/index/hooks"), sp_cstr_as_str(it->remote.hook)))),
      sp_path_cwd(sp_fs_join_path(mem, remote, sp_str_lit("hooks/pre-receive"))),
      SP_FS_ATOMIC_REPLACE));
  }

  sp_atomic_s32_t cancelled = sp_zero;
  sp_carr_for(it->actions, at) {
    txn_action_t action = it->actions[at];
    if (action.kind == TXN_ACTION_NONE) {
      break;
    }

    switch (action.kind) {
      case TXN_ACTION_PUBLISH: {
        spn_index_release_t rel = {
          .name = spn_intern_cstr(action.publish.name),
          .version = action.publish.version,
          .source = {
            .kind = SPN_PKG_ROOT_GIT,
            .git = { .url = sp_str_lit("https://example.com/pkg.git"), .rev = sp_str_lit("abc123") },
          },
        };
        sp_da_init(mem, rel.deps);
        sp_da_init(mem, rel.targets);
        publish_job_t job = { .index = &index, .rel = &rel, .cancelled = &cancelled };
        if (action.publish.contended) {
          sp_try(test_run_contended(t, sp_fmt(mem, "{}.lock", sp_fmt_str(clone)).value, run_publish, &job, &job.done, true));
        }
        else {
          run_publish(&job);
        }
        sp_expect_eq(t, action.publish.err, job.err);
        break;
      }
      case TXN_ACTION_SYNC: {
        spn_git_failure_t failure = sp_zero;
        sp_expect_eq(t, action.sync.err, spn_index_sync(&index, spn_index_plan(&index, false), &cancelled, &failure));
        break;
      }
      case TXN_ACTION_ADVANCE_REMOTE: {
        git_repo_pull(seed, remote, "main");
        sp_str_t file = sp_fs_join_path(mem, seed, sp_cstr_as_str(action.write.file));
        sp_fs_create_dir(sp_fs_parent_path(file));
        sp_fs_write(file, sp_fmt(mem, "{}\n", sp_fmt_cstr(action.write.line)).value, SP_FS_WRITE_TRUNCATE);
        git_repo_stage_all(seed);
        git_repo_commit(seed, sp_str_lit("advance"));
        git_repo_push(seed, remote, "HEAD:refs/heads/main");
        break;
      }
      case TXN_ACTION_REMOVE_REMOTE: {
        sp_must_ok(t, sp_fs_remove_dir(remote));
        break;
      }
      case TXN_ACTION_APPEND_CLONE_FILE: {
        sp_str_t file = sp_fs_join_path(mem, clone, sp_cstr_as_str(action.write.file));
        sp_fs_write(file, sp_fmt(mem, "{}{}\n",
          sp_fmt_str(test_read_file(mem, file)),
          sp_fmt_cstr(action.write.line)).value, SP_FS_WRITE_TRUNCATE);
        break;
      }
      case TXN_ACTION_CREATE_CLONE_FILE: {
        sp_str_t file = sp_fs_join_path(mem, clone, sp_cstr_as_str(action.write.file));
        sp_fs_create_dir(sp_fs_parent_path(file));
        sp_fs_write(file, sp_fmt(mem, "{}\n", sp_fmt_cstr(action.write.line)).value, SP_FS_WRITE_TRUNCATE);
        break;
      }
      case TXN_ACTION_DETACH_HEAD: {
        sp_str_t head = sp_zero;
        sp_expect_eq(t, SPN_OK, spn_git_get_commit_full(mem, clone, sp_str_lit("HEAD"), &head));
        sp_expect_eq(t, SPN_OK, spn_git_checkout(clone, head));
        break;
      }
      case TXN_ACTION_NONE: {
        break;
      }
    }
  }

  sp_carr_for(it->expect.files, at) {
    txn_file_t file = it->expect.files[at];
    if (!file.file) {
      break;
    }
    sp_expect_eq(t, file.lines, remote_line_count(mem, remote, file.file));
  }
  sp_carr_for(it->expect.cached, at) {
    txn_file_t file = it->expect.cached[at];
    if (!file.file) {
      break;
    }
    sp_expect_eq(t, file.lines, index_test_count_lines(test_read_file(mem, sp_fs_join_path(mem, clone, sp_cstr_as_str(file.file)))));
  }
  if (it->expect.head) {
    sp_expect_str_eq(t, remote_head_message(mem, remote), sp_cstr_as_str(it->expect.head));
  }
  if (it->expect.witness) {
    sp_sys_file_meta_t witness = sp_zero;
    sp_expect_ok(t, sp_fs_get_link_metadata(sp_fs_join_path(mem, remote, sp_cstr_as_str(it->expect.witness)), &witness));
  }
  sp_sys_file_meta_t meta = sp_zero;
  if (!sp_fs_get_link_metadata(clone, &meta) && meta.kind == SP_FS_KIND_DIR) {
    sp_expect(t, !spn_git_is_dirty(clone, clone));
  }

  return SP_OK;
}
