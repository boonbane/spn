#include "unit.h"

typedef struct {
  spn_path_root_t root;
  const c8* sub;
  const c8* prefix;
  const c8* suffix;
} path_t;

typedef struct {
  const c8* name;
  spn_pkg_source_t source;
  path_t work;
  path_t store;
  path_t include;
} test_t;

static const test_t tests [] = {
  {
    .name = "root_package_lives_under_the_build_root",
    .source = SPN_PKG_SOURCE_ROOT,
    .work =    { .root = SPN_PATH_ROOT_PROJECT, .sub = "build/debug/.spn/A" },
    .store =   { .root = SPN_PATH_ROOT_PROJECT, .sub = "build/debug/store/A" },
    .include = { .root = SPN_PATH_ROOT_PROJECT, .sub = "build/debug/store/A/include" },
  },
  {
    .name = "file_package_lives_under_the_build_root",
    .source = SPN_PKG_SOURCE_FILE,
    .work =    { .root = SPN_PATH_ROOT_PROJECT, .sub = "build/debug/.spn/A" },
    .store =   { .root = SPN_PATH_ROOT_PROJECT, .sub = "build/debug/store/A" },
    .include = { .root = SPN_PATH_ROOT_PROJECT, .sub = "build/debug/store/A/include" },
  },
  {
    .name = "index_package_lives_under_the_build_and_store_dirs",
    .source = SPN_PKG_SOURCE_INDEX,
    .work =    { .root = SPN_PATH_ROOT_STORAGE, .prefix = "cache/build/core/A/" },
    .store =   { .root = SPN_PATH_ROOT_STORAGE, .prefix = "cache/store/core/A/" },
    .include = { .root = SPN_PATH_ROOT_STORAGE, .prefix = "cache/store/core/A/", .suffix = "/include" },
  },
};

static sp_err_t expect_path(sp_test_t* t, spn_path_t actual, const path_t* expect) {
  sp_expect_eq(t, actual.root, expect->root);
  if (expect->sub) {
    sp_expect_str_eq_c(t, actual.sub, expect->sub);
  }
  if (expect->prefix) {
    sp_expect(t, sp_str_starts_with(actual.sub, sp_cstr_as_str(expect->prefix)));
  }
  if (expect->suffix) {
    sp_expect(t, sp_str_ends_with(actual.sub, sp_cstr_as_str(expect->suffix)));
  }
  return SP_OK;
}

sp_test_each(unit_paths, init, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  graph_t graph = { .pkgs = { { .name = "A", .source = it->source } } };
  units_t* u = units_new(t, &graph);

  spn_pkg_unit_t* unit = SP_NULLPTR;
  sp_must_eq(t, SPN_OK, units_add_package(u, "A", &unit));
  sp_try(expect_path(t, unit->paths.work, &it->work));
  sp_try(expect_path(t, unit->paths.store, &it->store));
  sp_try(expect_path(t, unit->paths.include, &it->include));
  return SP_OK;
}
