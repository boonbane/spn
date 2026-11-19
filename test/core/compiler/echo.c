#include "compiler.h"
#include "compiler/push.h"

typedef struct {
  const c8* out;
} expect_t;

typedef struct {
  const c8* name;
  const c8* out;
  const c8* err;
  const c8* echo;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "streams_merge_without_an_echo", .out = "A\n", .err = "E\n", .expect = { .out = "E\nA\n" } },
  { .name = "first_line_is_dropped", .out = "A\nB\n", .echo = "", .expect = { .out = "B\n" } },
  { .name = "lone_echo_is_silent", .out = "A\n", .echo = "" },
  { .name = "stderr_leads_stdout", .out = "A\nB\n", .err = "E\n", .echo = "", .expect = { .out = "E\nB\n" } },
  { .name = "stderr_survives_alone", .err = "E\n", .echo = "", .expect = { .out = "E\n" } },
  { .name = "prefix_selects_the_line", .out = "B\n P A\nC\n", .echo = " P ", .expect = { .out = "B\nC\n" } },
  { .name = "unmatched_prefix_keeps_everything", .out = "B\n", .echo = " P ", .expect = { .out = "B\n" } },
};

sp_test_each(invocation, echo, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_test_skip_on_win32();
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t dir = test_dir_str(t);
  sp_must_ok(t, sp_fs_write(sp_fs_join_path(mem, dir, sp_str_lit("out")), sp_cstr_as_str(it->out), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms));
  sp_must_ok(t, sp_fs_write(sp_fs_join_path(mem, dir, sp_str_lit("err")), sp_cstr_as_str(it->err), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms));

  spn_invocation_t invocation = {
    .program = spn_arg_path((spn_path_t) { .sub = sp_str_lit("/bin/sh") }),
    .cwd = { .sub = dir },
  };
  spn_cc_push_path(mem, &invocation, (spn_path_t) { .sub = test_repo_path(mem, sp_str_lit("test/core/compiler/echo/stub.sh")) });
  if (it->echo) {
    sp_opt_set(invocation.echo, sp_cstr_as_str(it->echo));
  }

  spn_children_t children = sp_zero;
  spn_invocation_result_t run = spn_invocation_run(&spn.roots, &children, &invocation);
  sp_expect_eq(t, run.result.status.exit_code, 0);
  sp_expect_str_eq_c(t, run.result.out, it->expect.out);
  return SP_OK;
}
