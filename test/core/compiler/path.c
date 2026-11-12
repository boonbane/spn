#include "compiler.h"
#include "compiler/push.h"

typedef struct {
  const c8* prefix;
} expect_t;

typedef struct {
  const c8* name;
  const c8* path [SPN_DIR_LIST_MAX];
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "program_dir_leads", .expect = { .prefix = "/bin:" } },
  { .name = "path_follows_the_program_dir", .path = { "/T", "/U" }, .expect = { .prefix = "/bin:/T:/U:" } },
};

sp_test_each(invocation, path, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_test_skip_on_win32();
  sp_mem_t mem = sp_test_arena(t);

  spn_invocation_t invocation = {
    .program = spn_arg_path((spn_path_t) { .sub = sp_str_lit("/bin/sh") }),
    .cwd = { .sub = test_dir_str(t) },
  };
  spn_cc_push_c(mem, &invocation, "-c");
  spn_cc_push_c(mem, &invocation, "printf %s \"$PATH\"");
  sp_carr_for_until(it->path, at, it->path[at]) {
    invocation.path.items[invocation.path.count++] = test_arg_path(it->path[at]);
  }

  spn_invocation_result_t run = spn_invocation_run(&spn.roots, &invocation);
  sp_expect_eq(t, run.result.status.exit_code, 0);
  sp_str_t prefix = sp_cstr_as_str(it->expect.prefix);
  sp_expect_str_eq(t, sp_str_prefix(run.result.out, sp_min(run.result.out.len, prefix.len)), prefix);
  return SP_OK;
}
