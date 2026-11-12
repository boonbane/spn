#include "compiler.h"
#include "compiler/push.h"

#define BATCH_TEST_MAX_ARGS 4

typedef struct {
  const c8* out;
  sp_err_t err;
} expect_t;

typedef struct {
  const c8* name;
  const c8* suffix;
  const c8* args [BATCH_TEST_MAX_ARGS];
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "plain", .args = { "A", "-B", "C/D" }, .expect = { .out = "A -B C/D\r\n" } },
  { .name = "operators", .args = { "A&B", "A|B", "A<B>C", "(A)" }, .expect = { .out = "\"A&B\" \"A|B\" \"A<B>C\" \"(A)\"\r\n" } },
  { .name = "quote", .args = { "\"A\"" }, .expect = { .out = "\"\"\"A\"\"\"\r\n" } },
  { .name = "quote_after_backslash", .args = { "A\\\"B" }, .expect = { .out = "\"A\\\\\"\"B\"\r\n" } },
  { .name = "percent", .args = { "%PATH%" }, .expect = { .out = "\"%PATH%\"\r\n" } },
  { .name = "bang", .args = { "!PATH!" }, .expect = { .out = "\"!PATH!\"\r\n" } },
  { .name = "caret", .args = { "^A" }, .expect = { .out = "\"^A\"\r\n" } },
  { .name = "trailing_backslash", .args = { "A\\" }, .expect = { .out = "\"A\\\\\"\r\n" } },
  { .name = "space", .args = { "A B" }, .expect = { .out = "\"A B\"\r\n" } },
  { .name = "equals", .args = { "A=B" }, .expect = { .out = "\"A=B\"\r\n" } },
  { .name = "trailing_dot", .suffix = " .", .args = { "A&B" }, .expect = { .out = "\"A&B\"\r\n" } },
  { .name = "line_feed", .args = { "A\nB" }, .expect = { .err = SP_ERR_SYS_INVALID } },
  { .name = "carriage_return", .args = { "A\rB" }, .expect = { .err = SP_ERR_SYS_INVALID } },
};

sp_test_each(batch, args, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_test_skip_on_linux();
  sp_test_skip_on_macos();
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t script = test_repo_path(mem, sp_str_lit("test/core/compiler/batch/echo.cmd"));
  spn_invocation_t invocation = {
    .program = spn_arg_path((spn_path_t) { .sub = sp_str_concat(mem, script, sp_cstr_as_str(it->suffix)) }),
    .cwd = { .sub = test_dir_str(t) },
  };
  u32 count = 0;
  sp_carr_detect_len(it->args, count, it->args[count]);
  sp_for(at, count) {
    spn_cc_push_c(mem, &invocation, it->args[at]);
  }

  spn_invocation_result_t run = spn_invocation_run(&spn.roots, &invocation);
  sp_expect_eq(t, run.result.error, it->expect.err);
  sp_expect_str_eq_c(t, run.result.out, it->expect.out);
  return SP_OK;
}

sp_test(batch, bare_name) {
  sp_test_skip_on_linux();
  sp_test_skip_on_macos();
  sp_ps_config_t config = { .command = sp_str_lit("echo.cmd") };
  sp_ps_output_t result = sp_ps_run(sp_test_arena(t), config);
  sp_expect_eq(t, result.error, SP_ERR_SYS_UNSUPPORTED);
  return SP_OK;
}
