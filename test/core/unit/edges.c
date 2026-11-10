#include "unit.h"

#define EDGES_TEST_MAX_EDGES 4

typedef struct {
  const c8* name;
  spn_target_kind_t kind;
  bool script;
  const c8* expect [EDGES_TEST_MAX_EDGES];
} test_t;

static const graph_t graph = {
  .pkgs = {
    {
      .name = "A",
      .deps = { { "D" }, { "T", SPN_DEP_KIND_TEST }, { "G", SPN_DEP_KIND_BUILD } },
      .targets = {
        { "B", .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM, .source = { "b.c" } },
        { "C", .kind = SPN_TARGET_KIND_CONFIGURE_METAPROGRAM, .source = { "c.c" } },
      },
    },
    { .name = "D" },
    { .name = "T" },
    { .name = "G" },
  },
};

static const test_t tests [] = {
  { .name = "exe_consumes_package_deps", .kind = SPN_TARGET_KIND_EXE, .expect = { "D" } },
  { .name = "test_consumes_package_and_test_deps", .kind = SPN_TARGET_KIND_TEST, .expect = { "D", "T" } },
  { .name = "example_consumes_its_own_package", .kind = SPN_TARGET_KIND_EXAMPLE, .expect = { "A", "D" } },
  { .name = "build_script_consumes_build_deps", .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM, .script = true, .expect = { "G" } },
  { .name = "configure_script_consumes_nothing", .kind = SPN_TARGET_KIND_CONFIGURE_METAPROGRAM, .script = true },
};

static spn_target_unit_t* configure_target(units_t* u) {
  sp_om_for(u->configure.targets, it) {
    return sp_om_at(u->configure.targets, it);
  }
  return SP_NULLPTR;
}

sp_test_each(unit_edges, consume, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &graph);
  spn_target_unit_t* target = SP_NULLPTR;
  if (it->script) {
    spn_pkg_unit_t* pkg = SP_NULLPTR;
    sp_must_eq(t, SPN_OK, units_add_package(u, "A", &pkg));
    target = it->kind == SPN_TARGET_KIND_CONFIGURE_METAPROGRAM ? configure_target(u) : units_find_target(u, "B", it->kind);
  }
  else {
    graph_root_t roots [] = { { "X", it->kind } };
    si_da(spn_target_unit_t*) targets = sp_zero;
    sp_must_eq(t, SPN_OK, units_add_roots(u, roots, sp_carr_len(roots), &targets));
    target = targets[0];
  }
  sp_must(t, target);

  u32 count = 0;
  sp_carr_detect_len(it->expect, count, it->expect[count]);
  sp_must_eq(t, count, (u32)si_da_size(target->edges));
  sp_for(et, count) {
    sp_expect_str_eq_c(t, target->edges[et].pkg->pkg.manifest->name.str, it->expect[et]);
  }
  return SP_OK;
}
