#include "compiler.h"

typedef struct {
  const c8* path;
} expect_t;

typedef struct {
  const c8* name;
  const c8* program;
  const c8* dirs [SPN_DIR_LIST_MAX];
  const c8* path;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "path_follows_the_program_dir", .program = "/X/A", .dirs = { "/T", "/U" }, .path = "/P", .expect = { .path = "/X:/T:/U:/P" } },
};

sp_test_each(invocation, path, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;

  spn_invocation_t invocation = { .program = spn_arg_path(test_arg_path(it->program)) };
  sp_carr_for_until(it->dirs, at, it->dirs[at]) {
    invocation.path.items[invocation.path.count++] = test_arg_path(it->dirs[at]);
  }

  sp_str_t path = spn_invocation_path(&roots, mem, &invocation, spn_search_rules(SPN_OS_LINUX), sp_cstr_as_str(it->path));
  sp_expect_str_eq_c(t, path, it->expect.path);
  return SP_OK;
}
