#include "closure.h"

#include "unit/closure/closure.h"

typedef struct {
  const c8* pkg;
  bool private;
} expect_t;

typedef struct {
  const c8* name;
  closure_graph_test_t graph;
  expect_t expect [CLOSURE_TEST_MAX_NAMES];
} test_t;

static const test_t tests [] = {
  {
    .name = "walk_static_chain",
    .graph = {
      .root = "P1",
      .pkgs = {
        { .name = "P1", .deps = { { "D1" } } },
        { .name = "D1", .kind = SPN_LIB_KIND_STATIC, .deps = { { "D2" } } },
        { .name = "D2", .kind = SPN_LIB_KIND_STATIC },
      },
    },
    .expect = {
      { "D1" },
      { "D2" }
    },
  },
  {
    .name = "stop_at_shared",
    .graph = {
      .root = "P1",
      .pkgs = {
        { .name = "P1", .deps = { { "D1" } } },
        { .name = "D1", .kind = SPN_LIB_KIND_SHARED, .deps = { { "D2" } } },
        { .name = "D2", .kind = SPN_LIB_KIND_STATIC },
      },
    },
    .expect = {
      { "D1" }
    },
  },
  {
    .name = "dedupe_diamond",
    .graph = {
      .root = "P1",
      .pkgs = {
        { .name = "P1", .deps = { { "D1" }, { "D2" } } },
        { .name = "D1", .kind = SPN_LIB_KIND_STATIC, .deps = { { "D3" } } },
        { .name = "D2", .kind = SPN_LIB_KIND_STATIC, .deps = { { "D3" } } },
        { .name = "D3", .kind = SPN_LIB_KIND_STATIC },
      },
    },
    .expect = {
      { "D2" },
      { "D1" },
      { "D3" }
    },
  },
  {
    .name = "skip_unreferenced",
    .graph = {
      .root = "P1",
      .pkgs = {
        { .name = "P1", .deps = { { "D1" } } },
        { .name = "D1", .kind = SPN_LIB_KIND_STATIC },
        { .name = "D2", .kind = SPN_LIB_KIND_STATIC },
      },
    },
    .expect = {
      { "D1" }
    },
  },
  // Privacy inherits down the private edge: everything reached through a
  // private dep is private to the link unit
  {
    .name = "propagate_privacy",
    .graph = {
      .root = "P1",
      .pkgs = {
        { .name = "P1", .deps = { { "D1", .private = true } } },
        { .name = "D1", .kind = SPN_LIB_KIND_STATIC, .deps = { { "D2" } } },
        { .name = "D2", .kind = SPN_LIB_KIND_STATIC },
      },
    },
    .expect = {
      { "D1", .private = true },
      { "D2", .private = true }
    },
  },
};

sp_test_each(closure, walk, test_t, tests, .setup = spn_test_ctx_setup) {
  closure_graph_t g = build_graph(&it->graph);

  si_da(si_closure_entry_t) pkgs = closure_entries(g.mem, g.root, SI_CLOSURE_STOP_AT_SHARED, SI_CLOSURE_YIELD_PKG);

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].pkg);
  sp_must_eq(t, expected, si_da_size(pkgs));
  sp_for(row, expected) {
    sp_expect_str_eq_c(t, pkgs[row].pkg->pkg.manifest->name.str, it->expect[row].pkg);
    sp_expect_eq(t, it->expect[row].private, pkgs[row].private);
  }

  return SP_OK;
}
