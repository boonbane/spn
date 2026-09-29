#include "paths/paths_test.h"

typedef enum {
  STAGE_PARENT_DIR,
  STAGE_PARENT_MISSING,
  STAGE_PARENT_FILE,
} stage_parent_t;

typedef struct {
  const c8* name;
  stage_parent_t parent;
  bool err;
} stage_test_t;

static const stage_test_t stage_tests [] = {
  { .name = "claims_a_sibling_of_the_target" },
  { .name = "missing_parent_is_not_created", .parent = STAGE_PARENT_MISSING, .err = true },
  { .name = "file_as_parent_fails",          .parent = STAGE_PARENT_FILE,    .err = true },
};

sp_test_each(paths_stage, dir, stage_test_t, stage_tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  sp_must(t, spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_PROJECT, sp_test_dir(t)) == SPN_OK);
  spn_path_t target = { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_str_lit("P/C") };

  switch (it->parent) {
    case STAGE_PARENT_DIR: {
      sp_must_ok(t, sp_fs_create_dir_at(spn_path_at(&roots, spn_path_parent(target))));
      break;
    }
    case STAGE_PARENT_MISSING: {
      break;
    }
    case STAGE_PARENT_FILE: {
      sp_must_ok(t, sp_fs_create_file_at(spn_path_at(&roots, spn_path_parent(target))));
      break;
    }
  }

  spn_path_t staged = sp_zero;
  spn_err_t err = spn_path_stage_dir(mem, &roots, target, sp_str_lit("tmp"), &staged);
  sp_expect_eq(t, err != SPN_OK, it->err);
  if (it->err) {
    sp_expect(t, spn_path_empty(staged));
    sp_expect(t, !sp_fs_exists_at(spn_path_at(&roots, spn_path_parent(target))) || it->parent == STAGE_PARENT_FILE);
    return SP_OK;
  }

  sp_expect_eq(t, staged.root, target.root);
  sp_expect(t, sp_str_starts_with(staged.sub, target.sub));
  sp_expect(t, sp_str_ends_with(staged.sub, sp_str_lit("tmp")));
  sp_expect(t, sp_fs_is_dir_at(spn_path_at(&roots, staged)));
  return SP_OK;
}

sp_test(paths_stage, claims_are_distinct) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  sp_must(t, spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_PROJECT, sp_test_dir(t)) == SPN_OK);
  spn_path_t target = { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_str_lit("C") };

  spn_path_t a = sp_zero;
  spn_path_t b = sp_zero;
  sp_must(t, spn_path_stage_dir(mem, &roots, target, sp_str_lit("tmp"), &a) == SPN_OK);
  sp_must(t, spn_path_stage_dir(mem, &roots, target, sp_str_lit("tmp"), &b) == SPN_OK);
  sp_expect(t, !spn_path_equal(a, b));
  sp_expect(t, sp_fs_is_dir_at(spn_path_at(&roots, a)));
  sp_expect(t, sp_fs_is_dir_at(spn_path_at(&roots, b)));
  return SP_OK;
}
