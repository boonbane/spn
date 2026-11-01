#include "unit.h"

#include "manifest.h"

#define EDGES_TEST_MAX_EDGES 4

typedef struct {
  const c8* edges [EDGES_TEST_MAX_EDGES];
} expect_t;

typedef struct {
  const c8* name;
  spn_target_kind_t kind;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "exe_edges_are_its_package_deps",
    .kind = SPN_TARGET_KIND_EXE,
    .expect = { .edges = { "D" } },
  },
  {
    .name = "example_consumes_its_own_package",
    .kind = SPN_TARGET_KIND_EXAMPLE,
    .expect = { .edges = { "A", "D" } },
  },
};

sp_test_each(unit_edges, target, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  unit_graph_test_t graph = {
    .pkgs = {
      { .name = "A", .deps = { { "D" } } },
      { .name = "D" },
    },
  };
  spn_session_t* s = build_session(mem, &graph);
  test_manifest_target(s->mem, test_manifest(s, find_pkg_id(s, &graph, "A")), sp_str_lit("app"), it->kind);
  spn_target_unit_t* target = SP_NULLPTR;
  sp_must_eq(t, SPN_OK, test_plan(s, sp_str_lit("app"), it->kind, &target));

  u32 count = 0;
  sp_carr_detect_len(it->expect.edges, count, it->expect.edges[count]);
  sp_must_eq(t, count, (u32)si_da_size(target->edges));
  sp_for(et, count) {
    sp_expect_str_eq_c(t, target->edges[et].pkg->pkg.manifest->name.str, it->expect.edges[et]);
  }
  return SP_OK;
}
