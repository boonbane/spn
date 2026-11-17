#include "unit.h"

typedef struct {
  spn_host_refusal_t reason;
  const c8* pkg;
  const c8* lib;
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t root;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "refusal_names_the_declaring_package",
    .graph = {
      .host = { SPN_ARCH_ARM64, SPN_OS_LINUX, SPN_ABI_GNU },
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", static_only, .source = { "d.c" }, .host_deps = { "H" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .reason = SPN_HOST_REFUSAL_TARGET, .pkg = "D", .lib = "H" },
  },
  {
    .name = "build_dep_host_deps_refused_in_scripts",
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
    .expect = { .reason = SPN_HOST_REFUSAL_SCRIPT, .pkg = "D", .lib = "H" },
  },
};

sp_test_each(unit_host, refusal, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, &it->root, 1, &targets));
  sp_must_eq(t, SPN_ERR_TARGET_HOST_DEPS, units_plan(u, targets));

  sp_da(spn_event_t) errs = units_errs(u);
  sp_must_eq(t, 1, (u32)sp_da_size(errs));
  spn_err_host_deps_t* refused = &errs[0].err.host_deps;
  sp_expect_eq(t, SPN_ERR_TARGET_HOST_DEPS, errs[0].err.kind);
  sp_expect_eq(t, it->expect.reason, refused->reason);
  sp_expect_str_eq_c(t, refused->pkg, it->expect.pkg);
  sp_must_eq(t, 1, (u32)sp_da_size(refused->libs));
  sp_expect_str_eq_c(t, refused->libs[0], it->expect.lib);
  return SP_OK;
}
