#include "compiler.h"
#include "compiler/push.h"

typedef struct {
  const c8* out;
} expect_t;

typedef struct {
  const c8* name;
  s32 rc;
  const c8* progress;
  const c8* err;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "passes",
    .progress = "AB",
    .err = "E\n",
  },
  {
    .name = "fails",
    .rc = 3,
    .progress = "ABCD",
    .err = "E\n",
    .expect = { .out = "E\n" },
  },
  {
    .name = "silent",
  },
};

sp_test_each(invocation, run_progress, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_test_skip_on_win32();
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t dir = sp_test_dir(t);
  sp_must_ok(t, sp_fs_create_file_str(sp_fs_join_path(mem, dir, sp_str_lit("progress")), sp_str_view(it->progress)));
  sp_must_ok(t, sp_fs_create_file_str(sp_fs_join_path(mem, dir, sp_str_lit("err")), sp_str_view(it->err)));

  spn_invocation_t invocation = {
    .program = spn_arg_path((spn_path_t) { .sub = sp_str_lit("/bin/sh") }),
    .cwd = { .sub = dir },
  };
  spn_cc_push_path(mem, &invocation, (spn_path_t) { .sub = test_repo_path(mem, sp_str_lit("test/core/compiler/run/stub.sh")) });
  spn_cc_push_fmt(mem, &invocation, "{}", sp_fmt_int(it->rc));
  spn_cc_push_env(mem, &invocation, SPN_ENV_ZIG_LIBC, spn_arg_lit(sp_str_lit("A")));

  sp_io_dyn_mem_writer_t sink = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &sink);
  spn_path_t log = { .sub = sp_fs_join_path(mem, dir, sp_str_lit("log")) };
  spn_invocation_result_t run = spn_invocation_run_progress(&invocation, log, &sink.base);

  sp_expect_eq(t, run.result.status.exit_code, it->rc);
  sp_expect_str_eq_c(t, sp_io_dyn_mem_writer_as_str(&sink), it->progress);
  sp_expect_str_eq_c(t, run.result.out, it->expect.out);
  sp_expect_eq(t, sp_da_size(invocation.env), 1);
  return SP_OK;
}
