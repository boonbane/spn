#include "paths/paths_test.h"

typedef struct {
  spn_path_root_t root;
  const c8* sub;
} expect_t;

typedef struct {
  const c8* name;
  const c8* str;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "relative_resolves_against_cwd",
    .str = "M",
    .expect = { SPN_PATH_ROOT_PROJECT, "M" },
  },
};

sp_test_each(paths_from_cwd, resolve, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  sp_must_eq(t, SPN_OK, spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_PROJECT, sp_path_from_str(sp_str_lit("."))));
  spn_path_t path = spn_path_from_cwd(mem, &roots, sp_cstr_as_str(it->str));
  spn_path_roots_close(&roots);
  sp_expect_eq(t, path.root, it->expect.root);
  sp_expect_str_eq_c(t, path.sub, it->expect.sub);
  return SP_OK;
}
