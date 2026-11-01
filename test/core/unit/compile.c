#include "unit.h"

#include "paths/paths.h"

#define COMPILE_TEST_MAX_ARGS 6

typedef struct {
  const c8* prefix;
  const c8* path;
} arg_t;

typedef struct {
  arg_t args [COMPILE_TEST_MAX_ARGS];
  const c8* absent [COMPILE_TEST_MAX_ARGS];
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t root;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "includes_go_package_then_target_then_deps",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .include = { "inc" }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .include = { "tinc" } } } },
      { .name = "B", .publish = { "b.h" } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect.args = {
      { "-I", "pkg/P/inc" },
      { "-I", "pkg/P/tinc" },
      { "-I", "build/debug/store/B/include" },
    },
  },
  {
    .name = "defines_go_facts_then_package_then_target",
    .graph.pkgs = {
      { .name = "P", .define = { "X" }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .define = { "Y" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect.args = { { "-DSPN_BUILD" }, { "-DX" }, { "-DY" } },
  },
  {
    .name = "dep_public_defines_reach_consumers",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .options = { { "X", .value = true, .define = "BX", .public = true }, { "Y", .value = true, .define = "BY" } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .args = { { "-DBX" } }, .absent = { "-DBY" } },
  },
  {
    .name = "libs_are_pic",
    .graph.pkgs = { { .name = "P", .targets = { { "A", static_only, .source = { "a.c" } } } } },
    .root = { "A", SPN_TARGET_KIND_LIB },
    .expect.args = { { "-fPIC" } },
  },
  {
    .name = "exes_are_not_pic",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect.absent = { "-fPIC" },
  },
};

static bool arg_matches(spn_arg_t arg, arg_t expect) {
  if (!sp_str_equal_cstr(arg.prefix, expect.prefix)) {
    return false;
  }
  if (!expect.path) {
    return spn_path_empty(arg.path);
  }
  return sp_str_ends_with(arg.path.sub, sp_cstr_as_str(expect.path));
}

sp_test_each(unit_compile, args, test_t, tests, .setup = spn_test_ctx_setup) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, &it->root, 1, &targets));
  sp_must_eq(t, SPN_OK, units_plan(u, targets));
  sp_must_eq(t, 1, (u32)sp_da_size(targets[0]->objects));
  sp_da(spn_arg_t) args = targets[0]->objects[0]->plan.base.args;

  u32 expected = 0;
  sp_carr_detect_len(it->expect.args, expected, it->expect.args[expected].prefix);
  u32 at = 0;
  sp_for(et, expected) {
    sp_test_kv_c(t, "arg", it->expect.args[et].prefix);
    while (at < sp_da_size(args) && !arg_matches(args[at], it->expect.args[et])) {
      at++;
    }
    sp_must(t, at < sp_da_size(args));
    at++;
  }

  u32 absent = 0;
  sp_carr_detect_len(it->expect.absent, absent, it->expect.absent[absent]);
  sp_for(et, absent) {
    sp_test_kv_c(t, "arg", it->expect.absent[et]);
    sp_da_for(args, at) {
      sp_expect(t, !arg_matches(args[at], (arg_t) { .prefix = it->expect.absent[et] }));
    }
  }
  return SP_OK;
}
