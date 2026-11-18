#include "unit.h"

typedef struct {
  const c8* archive;
  const c8* link;
  const c8* implib;
  bool exports;
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t root;
  spn_target_kind_t kind;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "exe_links",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_EXE,
    .expect = { .link = "A" },
  },
  {
    .name = "exe_on_windows_takes_the_extension",
    .graph = {
      .os = SPN_OS_WINDOWS,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_EXE,
    .expect = { .link = "A.exe" },
  },
  {
    .name = "exe_on_freestanding_is_an_elf",
    .graph = {
      .os = SPN_OS_FREESTANDING,
      .abi = SPN_ABI_BARE,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_EXE,
    .expect = { .link = "A.elf" },
  },
  {
    .name = "exe_on_freestanding_wasm_is_a_module",
    .graph = {
      .arch = SPN_ARCH_WASM32,
      .os = SPN_OS_FREESTANDING,
      .abi = SPN_ABI_BARE,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_EXE,
    .expect = { .link = "A.wasm" },
  },
  {
    .name = "static_lib_archives",
    .graph.pkgs = { { .name = "P", .targets = { { "A", static_only, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .archive = "libA.a" },
  },
  {
    .name = "static_lib_on_windows_gnu_keeps_the_ar_name",
    .graph = {
      .os = SPN_OS_WINDOWS,
      .pkgs = { { .name = "P", .targets = { { "A", static_only, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .archive = "libA.a" },
  },
  {
    .name = "shared_lib_links_with_exports",
    .graph.pkgs = { { .name = "P", .targets = { { "A", shared_only, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .link = "libA.so", .exports = true },
  },
  {
    .name = "shared_lib_on_macos_is_a_dylib",
    .graph = {
      .os = SPN_OS_MACOS,
      .pkgs = { { .name = "P", .targets = { { "A", shared_only, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .link = "libA.dylib", .exports = true },
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
    .expect = { .link = "A.dll", .implib = "A.dll.lib", .exports = true },
  },
  {
    .name = "root_lib_emits_the_kind_it_links_as",
    .graph.pkgs = { { .name = "P", .targets = { { "A", both_kinds, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .archive = "libA.a" },
  },
  {
    .name = "pinned_root_lib_emits_its_shared_kind",
    .graph.pkgs = { { .name = "P", .config = SPN_LIB_KIND_SHARED, .targets = { { "A", both_kinds, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .link = "libA.so", .exports = true },
  },
  {
    .name = "emit_lists_the_kinds_a_root_lib_builds",
    .graph = {
      .emit = { .shared = true, .static_lib = true },
      .pkgs = { { .name = "P", .targets = { { "A", both_kinds, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .archive = "libA.a", .link = "libA.so", .exports = true },
  },
  {
    .name = "emit_shared_alone_builds_no_archive",
    .graph = {
      .emit = { .shared = true },
      .pkgs = { { .name = "P", .targets = { { "A", both_kinds, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .link = "libA.so", .exports = true },
  },
  {
    .name = "lib_with_one_kind_ignores_emit",
    .graph = {
      .emit = { .shared = true },
      .pkgs = { { .name = "P", .targets = { { "A", static_only, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .archive = "libA.a" },
  },
  {
    .name = "emit_for_msvc_keeps_archive_and_implib_apart",
    .graph = {
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .driver = SPN_CC_DRIVER_ZIG,
      .runtime = SPN_RUNTIME_STATIC,
      .emit = { .shared = true, .static_lib = true },
      .pkgs = { { .name = "P", .targets = { { "A", both_kinds, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .archive = "A.lib", .link = "A.dll", .implib = "A.dll.lib", .exports = true },
  },
  {
    .name = "emit_does_not_reach_an_unselected_lib",
    .graph = {
      .emit = { .shared = true },
      .pkgs = { { .name = "P", .targets = {
        { "E", .kind = SPN_TARGET_KIND_EXE, .source = { "e.c" }, .deps = { "A" } },
        { "A", both_kinds, .source = { "a.c" } },
      } } },
    },
    .root = { "E", SPN_TARGET_KIND_EXE },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .archive = "libA.a" },
  },
  {
    .name = "emit_without_a_loader_only_archives",
    .graph = {
      .abi = SPN_ABI_MUSL,
      .linkage = SPN_LIB_KIND_STATIC,
      .libc = SPN_RUNTIME_STATIC,
      .emit = { .shared = true },
      .pkgs = { { .name = "P", .targets = { { "A", both_kinds, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .archive = "libA.a" },
  },
  {
    .name = "shared_root_lib_on_wasi_is_a_module",
    .graph = {
      .os = SPN_OS_WASI,
      .abi = SPN_ABI_MUSL,
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = { { .name = "P", .targets = { { "A", shared_only, .source = { "a.c" } } } } },
    },
    .kind = SPN_TARGET_KIND_LIB,
    .expect = { .link = "A.wasm", .exports = true },
  },
  {
    .name = "build_script_is_a_reactor",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM, .source = { "a.c" } } } } },
    .kind = SPN_TARGET_KIND_BUILD_METAPROGRAM,
    .expect = { .link = "A.wasm", .exports = true },
  },
};

sp_test_each(unit_emit, artifact, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  if (it->kind == SPN_TARGET_KIND_BUILD_METAPROGRAM) {
    spn_pkg_unit_t* pkg = SP_NULLPTR;
    sp_must_eq(t, SPN_OK, units_add_package(u, "P", &pkg));
  }
  else {
    graph_root_t root = it->root.name ? it->root : (graph_root_t) { "A", it->kind };
    sp_must_eq(t, SPN_OK, units_add_roots(u, &root, 1, &targets));
  }
  sp_must_eq(t, SPN_OK, units_plan(u, targets));

  spn_target_unit_t* unit = units_find_target(u, "A", it->kind);
  sp_must(t, unit);
  spn_target_emit_t* emit = &unit->emit;
  sp_expect_str_eq_c(t, emit->archive.output, it->expect.archive ? it->expect.archive : "");
  sp_expect_eq(t, it->expect.archive != SP_NULLPTR, !sp_opt_is_null(emit->archive.plan));
  sp_expect_str_eq_c(t, emit->link.output, it->expect.link ? it->expect.link : "");
  sp_expect_str_eq_c(t, emit->link.implib, it->expect.implib ? it->expect.implib : "");
  sp_expect_eq(t, it->expect.link != SP_NULLPTR, !sp_opt_is_null(emit->link.plan));
  sp_expect_eq(t, it->expect.exports, !sp_opt_is_null(emit->link.exports));
  return SP_OK;
}
