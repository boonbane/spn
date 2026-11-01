#include "unit.h"

#define STAGED_TEST_MAX_LIBS 4

typedef struct {
  const c8* exe;
  const c8* libs [STAGED_TEST_MAX_LIBS];
} closure_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t roots [UNIT_TEST_MAX_ROOTS];
  closure_t expect [UNIT_TEST_MAX_ROOTS];
} test_t;

static const test_t tests [] = {
  {
    .name = "bin_stages_shared_deps_beside_it",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A", { "build/debug/libB.so" } } },
  },
  {
    .name = "static_dep_not_staged",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", static_only, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A" } },
  },
  {
    .name = "test_stages_under_test_dir",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "T", .kind = SPN_TARGET_KIND_TEST, .source = { "t.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .roots = { { "T", SPN_TARGET_KIND_TEST } },
    .expect = { { "build/debug/test/T", { "build/debug/test/libB.so" } } },
  },
  {
    .name = "example_stages_under_example_dir",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "E", .kind = SPN_TARGET_KIND_EXAMPLE, .source = { "e.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .roots = { { "E", SPN_TARGET_KIND_EXAMPLE } },
    .expect = { { "build/debug/example/E", { "build/debug/example/libB.so" } } },
  },
  {
    .name = "script_stages_at_the_root",
    .graph.pkgs = {
      { .name = "P", .targets = { { "S", .kind = SPN_TARGET_KIND_SCRIPT, .source = { "s.c" } } } },
    },
    .roots = { { "S", SPN_TARGET_KIND_SCRIPT } },
    .expect = { { "build/debug/S" } },
  },
  {
    .name = "shared_sibling_staged",
    .graph.pkgs = {
      { .name = "P", .targets = {
        { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .deps = { "L" } },
        { "L", shared_only, .source = { "l.c" } },
      } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A", { "build/debug/libL.so" } } },
  },
  // Staging chases through shared boundaries the link closure stops at: a
  // shared lib's own shared deps must sit next to the executable too
  {
    .name = "chase_through_shared",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .deps = { { "C" } }, .targets = { { "B", shared_only, .source = { "b.c" } } } },
      { .name = "C", .deps = { { "D" } }, .targets = { { "C", static_only, .source = { "c.c" } } } },
      { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A", { "build/debug/libD.so", "build/debug/libB.so" } } },
  },
  {
    .name = "no_link_shared_not_staged",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .no_link = true, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = { { "build/debug/A" } },
  },
  {
    .name = "lib_root_not_staged",
    .graph.pkgs = {
      { .name = "P", .targets = { { "L", static_only, .source = { "l.c" } } } },
    },
    .roots = { { "L", SPN_TARGET_KIND_LIB } },
  },
  {
    .name = "two_bins_each_list_a_shared_lib",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = {
        { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } },
        { "C", .kind = SPN_TARGET_KIND_EXE, .source = { "c.c" } },
      } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE }, { "C", SPN_TARGET_KIND_EXE } },
    .expect = {
      { "build/debug/A", { "build/debug/libB.so" } },
      { "build/debug/C", { "build/debug/libB.so" } },
    },
  },
};

sp_test_each(unit_staged, closure, test_t, tests, .setup = spn_test_ctx_setup) {
  units_t* u = units_new(t, &it->graph);
  u32 roots = 0;
  sp_carr_detect_len(it->roots, roots, it->roots[roots].name);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, it->roots, roots, &targets));
  sp_must_eq(t, SPN_OK, units_plan(u, targets));

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].exe);
  sp_must_eq(t, expected, (u32)si_da_size(u->plan.staged));
  sp_for(st, expected) {
    const closure_t* expect = &it->expect[st];
    spn_stage_closure_t* staged = &u->plan.staged[st];
    sp_test_kv_c(t, "exe", expect->exe);
    sp_expect_str_eq_c(t, staged->exe.path.sub, expect->exe);

    u32 libs = 0;
    sp_carr_detect_len(expect->libs, libs, expect->libs[libs]);
    sp_must_eq(t, libs, (u32)si_da_size(staged->libs));
    sp_for(lt, libs) {
      sp_expect_str_eq_c(t, staged->libs[lt].path.sub, expect->libs[lt]);
    }
  }
  return SP_OK;
}
