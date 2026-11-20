#include "index.h"

#include "index/tip.h"

typedef struct {
  bool fetch;
  bool held;
  spn_err_t err;
  const c8* rev;
  const c8* cached;
  bool failing;
} expect_t;

typedef struct {
  const c8* name;
  const c8* cached;
  bool aged;
  bool failing;
  bool force;
  bool cancelled;
  const c8* remote;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "never_read",
    .remote = "B",
    .expect = { .fetch = true, .rev = "B", .cached = "B" },
  },
  {
    .name = "fresh_is_served",
    .cached = "A",
    .remote = "B",
    .expect = { .rev = "A", .cached = "A" },
  },
  {
    .name = "aged_is_read",
    .cached = "A",
    .aged = true,
    .remote = "B",
    .expect = { .fetch = true, .rev = "B", .cached = "B" },
  },
  {
    .name = "force_reads_a_fresh_tip",
    .cached = "A",
    .force = true,
    .remote = "B",
    .expect = { .fetch = true, .rev = "B", .cached = "B" },
  },
  {
    .name = "failed_read_keeps_the_tip",
    .cached = "A",
    .aged = true,
    .expect = { .fetch = true, .err = SPN_ERROR, .rev = "A", .cached = "A", .failing = true },
  },
  {
    .name = "recent_failure_holds",
    .cached = "A",
    .aged = true,
    .failing = true,
    .remote = "B",
    .expect = { .held = true, .rev = "A", .cached = "A", .failing = true },
  },
  {
    .name = "force_reads_through_a_failure",
    .cached = "A",
    .aged = true,
    .failing = true,
    .force = true,
    .remote = "B",
    .expect = { .fetch = true, .rev = "B", .cached = "B" },
  },
  {
    .name = "cancelled_read_is_not_a_failure",
    .cached = "A",
    .aged = true,
    .cancelled = true,
    .remote = "B",
    .expect = { .fetch = true, .err = SPN_ERROR, .rev = "A", .cached = "A" },
  },
};

static spn_err_t reply(void* user, spn_git_route_t route, sp_mem_t mem, sp_str_t url, sp_atomic_s32_t* cancelled, sp_str_t* rev, spn_git_failure_t* failure) {
  const test_t* test = (const test_t*)user;
  *failure = sp_zero_s(spn_git_failure_t);
  if (!test->remote) {
    failure->kind = SPN_GIT_FAILED;
    return SPN_ERROR;
  }
  *rev = sp_cstr_as_str(test->remote);
  return SPN_OK;
}

sp_test_each(index_tip, read, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t dir = test_dir_str(t);
  sp_str_t read = sp_fs_join_path(mem, dir, sp_str_lit("spn-tip"));
  sp_str_t failed = sp_fs_join_path(mem, dir, sp_str_lit("spn-failed"));

  if (it->cached) {
    sp_fs_write(read, sp_cstr_as_str(it->cached), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms);
  }
  if (it->aged) {
    sp_path_t stamp = sp_path_from_str(read);
    sp_sys_timespec_t old = { .tv_sec = 1 };
    sp_must_eq(t, SP_OK, sp_sys_set_times_s(stamp.dir, stamp.sub, old, old));
  }
  if (it->failing) {
    sp_fs_write(failed, sp_str_lit(""), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms);
  }

  spn_index_info_t index = {
    .protocol = SPN_INDEX_PROTOCOL_GIT,
    .git.url = sp_str_lit("U"),
    .location.at = sp_path_from_str(dir),
    .refresh = 600,
    .remote = { .tip = reply, .user = (void*)it },
  };
  sp_atomic_s32_t cancelled = sp_zero;
  sp_atomic_s32_store(&cancelled, it->cancelled, SP_ATOMIC_SEQ_CST);

  si_index_tip_t tip = si_index_tip_plan(mem, &index, it->force);
  sp_expect_eq(t, it->expect.fetch, tip.fetch);
  sp_expect_eq(t, it->expect.held, tip.held);

  sp_str_t rev = tip.rev;
  if (tip.fetch) {
    spn_git_failure_t failure = sp_zero;
    sp_expect_eq(t, it->expect.err, si_index_tip_fetch(mem, &index, &cancelled, &rev, &failure));
  }
  sp_expect_str_eq_c(t, rev, it->expect.rev);
  sp_expect_str_eq_c(t, test_read_file(mem, read), it->expect.cached);

  sp_sys_file_meta_t meta = sp_zero;
  sp_expect_eq(t, it->expect.failing, !sp_fs_get_path_metadata(failed, &meta));
  return SP_OK;
}
