#include "unit.h"

#define WALK_TEST_MAX_UNITS 8

typedef enum {
  UNIT_PROGRAM,
  UNIT_METAPROGRAM,
  UNIT_CONFIGURE,
} where_t;

typedef struct {
  const c8* pkg;
  where_t where;
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t roots [UNIT_TEST_MAX_ROOTS];
  expect_t expect [WALK_TEST_MAX_UNITS];
} test_t;

static const test_t tests [] = {
  {
    .name = "package_chain_stays_in_program",
    .graph.pkgs = {
      { .name = "R", .deps = { { "A" } } },
      { .name = "A", .deps = { { "B" } } },
      { .name = "B" },
    },
    .roots = { { "X", SPN_TARGET_KIND_EXE } },
    .expect = { { "R" }, { "A" }, { "B" } },
  },
  {
    .name = "test_dep_of_test_target_included",
    .graph.pkgs = {
      { .name = "R", .deps = { { "T", SPN_DEP_KIND_TEST } } },
      { .name = "T" },
    },
    .roots = { { "X", SPN_TARGET_KIND_TEST } },
    .expect = { { "R" }, { "T" } },
  },
  {
    .name = "test_dep_of_exe_target_excluded",
    .graph.pkgs = {
      { .name = "R", .deps = { { "T", SPN_DEP_KIND_TEST } } },
      { .name = "T" },
    },
    .roots = { { "X", SPN_TARGET_KIND_EXE } },
    .expect = { { "R" } },
  },
  {
    .name = "test_dep_of_dep_excluded",
    .graph.pkgs = {
      { .name = "R", .deps = { { "A" } } },
      { .name = "A", .deps = { { "T", SPN_DEP_KIND_TEST } } },
      { .name = "T" },
    },
    .roots = { { "X", SPN_TARGET_KIND_TEST } },
    .expect = { { "R" }, { "A" } },
  },
  {
    .name = "build_script_lands_in_metabuild_with_its_build_deps",
    .graph.pkgs = {
      { .name = "R", .deps = { { "T", SPN_DEP_KIND_BUILD } }, .targets = { { "B", .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM, .source = { "b.c" } } } },
      { .name = "T" },
    },
    .roots = { { "X", SPN_TARGET_KIND_EXE } },
    .expect = { { "R" }, { "R", UNIT_METAPROGRAM }, { "T", UNIT_METAPROGRAM } },
  },
  {
    .name = "build_dep_without_build_script_excluded",
    .graph.pkgs = {
      { .name = "R", .deps = { { "T", SPN_DEP_KIND_BUILD } } },
      { .name = "T" },
    },
    .roots = { { "X", SPN_TARGET_KIND_EXE } },
    .expect = { { "R" } },
  },
  {
    .name = "configure_script_lands_in_configure_units_and_follows_no_edges",
    .graph.pkgs = {
      { .name = "R", .deps = { { "A" }, { "T", SPN_DEP_KIND_BUILD } }, .targets = { { "C", .kind = SPN_TARGET_KIND_CONFIGURE_METAPROGRAM, .source = { "c.c" } } } },
      { .name = "A" },
      { .name = "T" },
    },
    .roots = { { "X", SPN_TARGET_KIND_EXE } },
    .expect = { { "R" }, { "A" }, { "R", UNIT_CONFIGURE } },
  },
  // A build dep is consumed as a library, so only its package deps follow;
  // its own build deps belong to its build script, which nothing here roots
  {
    .name = "build_dep_follows_package_deps_only",
    .graph.pkgs = {
      { .name = "R", .deps = { { "T", SPN_DEP_KIND_BUILD } }, .targets = { { "B", .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM, .source = { "b.c" } } } },
      { .name = "T", .deps = { { "U" }, { "G", SPN_DEP_KIND_BUILD } } },
      { .name = "U" },
      { .name = "G" },
    },
    .roots = { { "X", SPN_TARGET_KIND_EXE } },
    .expect = { { "R" }, { "R", UNIT_METAPROGRAM }, { "T", UNIT_METAPROGRAM }, { "U", UNIT_METAPROGRAM } },
  },
  {
    .name = "dual_membership_gets_two_units",
    .graph.pkgs = {
      { .name = "R", .deps = { { "A" }, { "A", SPN_DEP_KIND_BUILD } }, .targets = { { "B", .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM, .source = { "b.c" } } } },
      { .name = "A" },
    },
    .roots = { { "X", SPN_TARGET_KIND_EXE } },
    .expect = { { "R" }, { "A" }, { "R", UNIT_METAPROGRAM }, { "A", UNIT_METAPROGRAM } },
  },
};

static u32 count_units(units_t* u, const expect_t* expect) {
  u32 count = 0;
  sp_om_for(u->units.packages, pt) {
    spn_pkg_unit_t* unit = sp_om_at(u->units.packages, pt);
    where_t where = unit->build == u->session.builds.metaprogram ? UNIT_METAPROGRAM : UNIT_PROGRAM;
    count += where == expect->where && sp_str_equal_cstr(unit->pkg.manifest->name.str, expect->pkg);
  }
  sp_om_for(u->configure.packages, pt) {
    spn_pkg_unit_t* unit = sp_om_at(u->configure.packages, pt);
    count += expect->where == UNIT_CONFIGURE && sp_str_equal_cstr(unit->pkg.manifest->name.str, expect->pkg);
  }
  return count;
}

sp_test_each(unit_walk, packages, test_t, tests, .setup = spn_test_ctx_setup) {
  units_t* u = units_new(t, &it->graph);
  u32 roots = 0;
  sp_carr_detect_len(it->roots, roots, it->roots[roots].name);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, it->roots, roots, &targets));

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].pkg);
  sp_must_eq(t, expected, (u32)(sp_om_size(u->units.packages) + sp_om_size(u->configure.packages)));
  sp_for(et, expected) {
    sp_test_kv_c(t, "pkg", it->expect[et].pkg);
    sp_expect_eq(t, 1, count_units(u, &it->expect[et]));
  }
  return SP_OK;
}
