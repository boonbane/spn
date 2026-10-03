#include "dag/dag_test.h"

typedef struct {
  paths_test_roots_t roots;
  spn_path_root_t root;
  const c8* sub;
} location_t;

typedef struct {
  bool equal;
} expect_t;

typedef struct {
  const c8* name;
  location_t a;
  location_t b;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "root_dir_changes_digest",
    .a = { .roots = { .project = "/A" }, .root = SPN_PATH_ROOT_PROJECT, .sub = "S" },
    .b = { .roots = { .project = "/B" }, .root = SPN_PATH_ROOT_PROJECT, .sub = "S" },
  },
  {
    .name = "sub_changes_digest",
    .a = { .roots = { .project = "/A" }, .root = SPN_PATH_ROOT_PROJECT, .sub = "S" },
    .b = { .roots = { .project = "/A" }, .root = SPN_PATH_ROOT_PROJECT, .sub = "T" },
  },
  {
    .name = "other_root_dir_keeps_digest",
    .a = { .roots = { .project = "/A", .store = "/C" }, .root = SPN_PATH_ROOT_STORE, .sub = "S" },
    .b = { .roots = { .project = "/B", .store = "/C" }, .root = SPN_PATH_ROOT_STORE, .sub = "S" },
    .expect = { .equal = true }
  },
};

static spn_dag_digest_t build_digest(const location_t* location) {
  spn_path_roots_t storage = sp_zero;
  const spn_path_roots_t* roots = paths_test_roots_build(location->roots, &storage);
  return spn_dag_path_digest(roots, (spn_path_t) { .root = location->root, .sub = sp_cstr_as_str(location->sub) });
}

sp_test_each(dag_path, digest, test_t, tests) {
  sp_expect_eq(t, it->expect.equal, spn_dag_digest_equal(build_digest(&it->a), build_digest(&it->b)));
  return SP_OK;
}
