#include "unit.h"

#define INCLUDES_TEST_MAX_EDGES 4

typedef struct {
  const c8* pkg;
  const c8* dirs [UNIT_TEST_MAX_STRS];
} edge_t;

typedef struct {
  edge_t edges [INCLUDES_TEST_MAX_EDGES];
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
    .expect.edges = { { "D" }, { "P" } },
  },
  {
    .name = "test_searches_its_published_include_last",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .publish = { "p.h" } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_TEST },
    .expect.edges = { { "D" }, { "P" } },
  },
  {
    .name = "example_searches_its_package_once",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .publish = { "p.h" } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_EXAMPLE },
    .expect.edges = { { "P" }, { "D" } },
  },
  {
    .name = "unpublished_package_is_not_searched",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } } },
      { .name = "D" },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect.edges = { { "D" } },
  },
  {
    .name = "sibling_subdirs_join_the_published_include",
    .graph.pkgs = {
      { .name = "P", .publish = { "p.h" }, .targets = {
        { "L", static_only },
        { "A", .kind = SPN_TARGET_KIND_EXE, .deps = { "L" }, .dep_include = { { "X" } } },
      } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect.edges = { { "P", { "X" } } },
  },
};

sp_test_each(unit_includes, search, test_t, tests, .setup = spn_test_ctx_setup) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, &it->root, 1, &targets));

  u32 count = 0;
  sp_carr_detect_len(it->expect.edges, count, it->expect.edges[count].pkg);
  sp_must_eq(t, count, (u32)si_da_size(targets[0]->includes));
  sp_for(et, count) {
    spn_include_edge_t* edge = &targets[0]->includes[et];
    sp_expect_str_eq_c(t, edge->pkg->pkg.manifest->name.str, it->expect.edges[et].pkg);
    sp_must_strs_eq(t, edge->dirs, si_da_size(edge->dirs), it->expect.edges[et].dirs);
  }
  return SP_OK;
}
