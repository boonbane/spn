#include "unit.h"

typedef struct {
  const c8* pkg;
  const c8* lib;
  bool script;
  const c8* candidates [UNIT_TEST_MAX_TOOLCHAINS + 1];
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t root;
  expect_t expect;
} refused_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t root;
} accepted_t;

static const refused_t refused_tests [] = {
  {
    .name = "names_the_declaring_package",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "D", .targets = { { "D", static_only, .source = { "d.c" }, .host_deps = { "H" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .pkg = "D", .lib = "H" },
  },
  {
    .name = "lists_toolchains_that_build_against_the_host",
    .graph = {
      .catalog = { { "G", .host_row = true }, { "Z" }, { "F", .host_row = true, .foreign = true } },
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .host_deps = { "H" } } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .pkg = "P", .lib = "H", .candidates = { "G" } },
  },
  {
    .name = "a_name_declared_twice_is_reported_once",
    .graph.pkgs = {
      { .name = "P", .host_deps = { "H" }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .host_deps = { "H" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .pkg = "P", .lib = "H" },
  },
  {
    .name = "build_dep_in_a_script",
    .graph.pkgs = {
      {
        .name = "P",
        .deps = { { "D", SPN_DEP_KIND_BUILD } },
        .targets = {
          { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } },
          { "B", .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM, .source = { "b.c" } },
        },
      },
      { .name = "D", .targets = { { "D", static_only, .source = { "d.c" }, .host_deps = { "H" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .pkg = "D", .lib = "H", .script = true },
  },
};

static const accepted_t accepted_tests [] = {
  {
    .name = "build_against_the_host",
    .graph = {
      .sdk_host = true,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .host_deps = { "H" } } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  {
    .name = "scripts_take_no_package_host_deps",
    .graph = {
      .sdk_host = true,
      .pkgs = {
        {
          .name = "P",
          .host_deps = { "H" },
          .targets = {
            { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } },
            { "B", .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM, .source = { "b.c" } },
          },
        },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  {
    .name = "lib_with_nothing_to_compile",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "D", .targets = { { "D", static_only, .host_deps = { "H" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
};

sp_test_each(unit_host, refused, refused_t, refused_tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, &it->root, 1, &targets));
  sp_must_eq(t, SPN_ERR_TARGET_HOST_DEPS, units_plan(u, targets));

  sp_da(spn_event_t) errs = units_errs(u);
  sp_must_eq(t, 1, (u32)sp_da_size(errs));
  spn_err_host_deps_t* refused = &errs[0].err.host_deps;
  sp_expect_eq(t, SPN_ERR_TARGET_HOST_DEPS, errs[0].err.kind);
  sp_expect_str_eq_c(t, refused->pkg, it->expect.pkg);
  sp_must_eq(t, 1, (u32)sp_da_size(refused->libs));
  sp_expect_str_eq_c(t, refused->libs[0], it->expect.lib);
  sp_expect_eq(t, it->expect.script, refused->script);
  sp_must_strs_eq(t, refused->candidates, sp_da_size(refused->candidates), it->expect.candidates);
  return SP_OK;
}

sp_test_each(unit_host, accepted, accepted_t, accepted_tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, &it->root, 1, &targets));
  sp_must_eq(t, SPN_OK, units_plan(u, targets));
  return SP_OK;
}
