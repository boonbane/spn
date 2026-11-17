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
      { .name = "P", .deps = { { "D" } }, .publish.include = { "p.h" }, .targets = { { "A", static_only } } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
    .expect.pkgs = { "D", "P" },
  },
  {
    .name = "test_searches_its_published_include_last",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .publish.include = { "p.h" } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_TEST },
    .expect.pkgs = { "D", "P" },
  },
  {
    .name = "example_searches_its_package_once",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .publish.include = { "p.h" } },
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
  {
    .name = "public_chain_is_searched",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } } },
      { .name = "B", .deps = { { "C", .public = true } } },
      { .name = "C", .deps = { { "D", .public = true } } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect.pkgs = { "B", "C", "D" },
  },
  {
    .name = "private_edge_hides_its_subtree",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } } },
      { .name = "B", .deps = { { "C" } } },
      { .name = "C", .deps = { { "D", .public = true } } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect.pkgs = { "B" },
  },
  {
    .name = "public_diamond_is_searched_once",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" }, { "C" } } },
      { .name = "B", .deps = { { "D", .public = true } } },
      { .name = "C", .deps = { { "D", .public = true } } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect.pkgs = { "B", "C", "D" },
  },
  {
    .name = "tool_searches_tool_deps_only",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" }, { "T", SPN_DEP_KIND_TOOL } }, .targets = { { "A", .kind = SPN_TARGET_KIND_TOOL }, { "L", static_only } } },
      { .name = "D" },
      { .name = "T" },
    },
    .root = { "A", SPN_TARGET_KIND_TOOL },
    .expect.pkgs = { "T" },
  },
  {
    .name = "tool_searches_package_deps_of_a_sibling_lib",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" }, { "T", SPN_DEP_KIND_TOOL } }, .targets = { { "A", .kind = SPN_TARGET_KIND_TOOL, .deps = { "L" } }, { "L", static_only } } },
      { .name = "D" },
      { .name = "T" },
    },
    .root = { "A", SPN_TARGET_KIND_TOOL },
    .expect.pkgs = { "T", "D" },
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
