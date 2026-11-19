#include "unit.h"

typedef struct {
  bool compile;
  bool link;
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t root;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "native_unset_is_on",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.cpp" } } } } },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .compile = true, .link = true },
  },
  {
    .name = "native_off_is_off",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.cpp" }, .exceptions = SPN_CXX_EXCEPTIONS_OFF } } } },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  {
    .name = "c_target_has_none",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .exceptions = SPN_CXX_EXCEPTIONS_ON } } } },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  {
    .name = "wasm_unset_is_off",
    .graph = {
      .arch = SPN_ARCH_WASM32,
      .os = SPN_OS_WASI,
      .abi = SPN_ABI_MUSL,
      .exceptions = { "-fA" },
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.cpp" } } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  {
    .name = "wasm_on_takes_a_declaring_row",
    .graph = {
      .arch = SPN_ARCH_WASM32,
      .os = SPN_OS_WASI,
      .abi = SPN_ABI_MUSL,
      .exceptions = { "-fA" },
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.cpp" }, .exceptions = SPN_CXX_EXCEPTIONS_ON } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .compile = true, .link = true },
  },
  {
    .name = "link_folds_its_static_libs",
    .graph = {
      .arch = SPN_ARCH_WASM32,
      .os = SPN_OS_WASI,
      .abi = SPN_ABI_MUSL,
      .exceptions = { "-fA" },
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", static_only, .source = { "d.cpp" }, .exceptions = SPN_CXX_EXCEPTIONS_ON } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .link = true },
  },
};

sp_test_each(unit_exceptions, plan, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, &it->root, 1, &targets));
  sp_must_eq(t, SPN_OK, units_plan(u, targets));
  sp_expect_eq(t, it->expect.compile, targets[0]->exceptions);
  sp_expect_eq(t, it->expect.link, targets[0]->link.cc.exceptions);
  return SP_OK;
}
