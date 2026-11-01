#include "unit.h"

typedef struct {
  const c8* output;
  const c8* implib;
  bool objects;
  bool link;
  bool archive;
  bool exports;
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  spn_target_kind_t kind;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "exe_links",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_EXE,
    .expect = { .output = "A", .link = true },
  },
  {
    .name = "exe_on_windows_takes_the_extension",
    .graph = {
      .os = SPN_OS_WINDOWS,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_EXE,
    .expect = { .output = "A.exe", .link = true },
  },
  {
    .name = "static_lib_archives",
    .graph.pkgs = { { .name = "P", .targets = { { "A", static_only, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .output = "libA.a", .archive = true },
  },
  {
    .name = "shared_lib_links_with_exports",
    .graph.pkgs = { { .name = "P", .targets = { { "A", shared_only, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .output = "libA.so", .link = true, .exports = true },
  },
  {
    .name = "shared_lib_for_msvc_has_an_implib",
    .graph = {
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .driver = SPN_CC_DRIVER_ZIG,
      .runtime = SPN_RUNTIME_STATIC,
      .pkgs = { { .name = "P", .targets = { { "A", shared_only, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .output = "A.dll", .implib = "A.lib", .link = true, .exports = true },
  },
  {
    .name = "object_lib_emits_its_objects",
    .graph.pkgs = { { .name = "P", .targets = { { "A", object_only, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .objects = true },
  },
  {
    .name = "source_lib_emits_nothing",
    .graph.pkgs = { { .name = "P", .targets = { { "A", source_only, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_LIB,
  },
  {
    .name = "build_script_is_a_reactor",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM,
    .expect = { .output = "A.wasm", .link = true, .exports = true },
  },
};

sp_test_each(unit_emit, artifact, test_t, tests, .setup = spn_test_ctx_setup) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  if (it->kind == SPN_TARGET_KIND_BUILD_METAPROGRAM) {
    spn_pkg_unit_t* pkg = SP_NULLPTR;
    sp_must_eq(t, SPN_OK, units_add_package(u, "P", &pkg));
  }
  else {
    graph_root_t roots [] = { { "A", it->kind } };
    sp_must_eq(t, SPN_OK, units_add_roots(u, roots, sp_carr_len(roots), &targets));
  }
  sp_must_eq(t, SPN_OK, units_plan(u, targets));

  spn_target_unit_t* unit = units_find_target(u, "A", it->kind);
  sp_must(t, unit);
  spn_target_emit_t* emit = &unit->emit;
  sp_expect_str_eq_c(t, emit->output, it->expect.output ? it->expect.output : "");
  sp_expect_str_eq_c(t, emit->implib, it->expect.implib ? it->expect.implib : "");
  sp_expect_eq(t, it->expect.objects, emit->objects);
  sp_expect_eq(t, it->expect.link, !sp_opt_is_null(emit->link));
  sp_expect_eq(t, it->expect.archive, !sp_opt_is_null(emit->archive));
  sp_expect_eq(t, it->expect.exports, !sp_opt_is_null(emit->exports));
  return SP_OK;
}
