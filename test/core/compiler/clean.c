#include "compiler.h"
#include "compiler/push.h"

typedef struct {
  s32 rc;
  bool scratch;
} expect_t;

typedef struct {
  const c8* name;
  const c8* var;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "host_variable_dropped", .var = "A", .expect = { .rc = 1 } },
  { .name = "temp_dir_is_the_scratch", .var = "TMPDIR", .expect = { .scratch = true } },
};

sp_test_each(invocation, clean, test_t, tests, .setup = spn_test_ctx_setup, .serial = true) {
  sp_test_skip_on_win32();
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t dir = test_dir_str(t);
  sp_str_t var = sp_cstr_as_str(it->var);
  sp_str_t saved = sp_str_copy(mem, sp_env_get(spn.env, var));
  sp_env_insert(spn.env, var, sp_str_lit("B"));

  spn_invocation_t invocation = {
    .program = spn_arg_path((spn_path_t) { .sub = sp_str_lit("/bin/sh") }),
    .cwd = { .sub = dir },
  };
  spn_cc_push_path(mem, &invocation, (spn_path_t) { .sub = test_repo_path(mem, sp_str_lit("test/core/compiler/clean/print.sh")) });
  spn_cc_push_str(mem, &invocation, var);

  spn_children_t children = sp_zero;
  spn_invocation_result_t run = spn_invocation_run(&spn.roots, &children, &invocation);
  if (sp_str_empty(saved)) {
    sp_env_erase(spn.env, var);
  }
  else {
    sp_env_insert(spn.env, var, saved);
  }

  sp_expect_eq(t, run.result.status.exit_code, it->expect.rc);
  sp_expect_str_eq(t, run.result.out, it->expect.scratch ? sp_fmt(mem, "{}\n", sp_fmt_str(dir)).value : sp_str_lit(""));
  return SP_OK;
}
