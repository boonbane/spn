#include "unit.h"

typedef struct {
  const c8* pkgs [UNIT_TEST_MAX_STRS];
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t root;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "lib_searches_its_published_include_last",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .publish = { "p.h" }, .targets = { { "A", static_only } } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
    .expect.pkgs = { "D", "P" },
  },
  {
    .name = "test_searches_its_published_include_last",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .publish = { "p.h" } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_TEST },
    .expect.pkgs = { "D", "P" },
  },
  {
    .name = "example_searches_its_package_once",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .publish = { "p.h" } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_EXAMPLE },
    .expect.pkgs = { "P", "D" },
  },
  {
    .name = "unpublished_package_is_not_searched",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect.pkgs = { "D" },
  },
};

sp_test_each(unit_includes, search, test_t, tests, .setup = spn_test_ctx_setup) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, &it->root, 1, &targets));

  u32 count = 0;
  sp_carr_detect_len(it->expect.pkgs, count, it->expect.pkgs[count]);
  sp_must_eq(t, count, (u32)si_da_size(targets[0]->includes));
  sp_for(pt, count) {
    sp_expect_str_eq_c(t, targets[0]->includes[pt]->pkg.manifest->name.str, it->expect.pkgs[pt]);
  }
  return SP_OK;
}
