#include "paths/paths_test.h"

typedef struct {
  spn_path_root_t root;
  const c8* sub;
} ref_t;

typedef struct {
  const c8* name;
  ref_t base;
  const c8* str;
  ref_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "relative_joins_the_base",
    .base = { SPN_PATH_ROOT_PROJECT, "D" },
    .str = "F",
    .expect = { SPN_PATH_ROOT_PROJECT, "D/F" },
  },
  {
    .name = "absolute_ignores_the_base_and_is_unrooted",
    .base = { SPN_PATH_ROOT_PROJECT, "D" },
    .str = "/A/F",
    .expect = { .sub = "/A/F" },
  },
  {
    .name = "relative_dotdot_is_kept_verbatim",
    .base = { SPN_PATH_ROOT_PROJECT, "D" },
    .str = "../F",
    .expect = { SPN_PATH_ROOT_PROJECT, "D/../F" },
  },
};

sp_test_each(paths_resolve, resolve, test_t, tests) {
  spn_path_t base = { .root = it->base.root, .sub = sp_cstr_as_str(it->base.sub) };
  spn_path_t path = spn_path_resolve(sp_test_arena(t), base, sp_cstr_as_str(it->str));
  sp_expect_eq(t, path.root, it->expect.root);
  sp_expect_str_eq_c(t, path.sub, it->expect.sub);
  return SP_OK;
}
