#include "paths/paths_test.h"

#define PATH_TEST_MAX_SUBS 4

typedef struct {
  const c8* name;
  paths_test_roots_t roots;
  spn_path_root_t root;
  const c8* subs [PATH_TEST_MAX_SUBS];
  const c8* expect;
} path_test_t;

static const path_test_t path_tests [] = {
  {
    .name = "root_is_the_root_dir",
    .roots = { .project = "/A" },
    .root = SPN_PATH_ROOT_PROJECT,
    .expect = "/A"
  },
  {
    .name = "join_from_root_copies_sub",
    .roots = { .project = "/A" },
    .root = SPN_PATH_ROOT_PROJECT,
    .subs = { "D" },
    .expect = "/A/D"
  },
  {
    .name = "join_descends",
    .roots = { .project = "/A" },
    .root = SPN_PATH_ROOT_PROJECT,
    .subs = { "D", "H" },
    .expect = "/A/D/H"
  },
  {
    .name = "join_keeps_multi_component_sub",
    .roots = { .storage = "/A/S" },
    .root = SPN_PATH_ROOT_STORAGE,
    .subs = { "D/E" },
    .expect = "/A/S/D/E"
  },
};

sp_test_each(paths_path, construct, path_test_t, path_tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t storage = sp_zero;
  const spn_path_roots_t* roots = paths_test_roots_build(it->roots, &storage);

  spn_path_t path = spn_path_from_root(it->root);
  sp_expect_eq(t, path.root, it->root);

  u32 count = 0;
  sp_carr_detect_len(it->subs, count, it->subs[count]);
  sp_for(st, count) {
    path = spn_path_join(mem, path, sp_str_view(it->subs[st]));
    sp_expect_eq(t, path.root, it->root);
  }

  sp_expect_str_eq_c(t, spn_path_str(roots, mem, path), it->expect);
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_path_root_t root;
  const c8* sub;
  const c8* expect;
} parent_test_t;

static const parent_test_t parent_tests [] = {
  { .name = "nested_drops_the_leaf",           .root = SPN_PATH_ROOT_PROJECT, .sub = "D/H", .expect = "D" },
  { .name = "top_level_is_the_root",           .root = SPN_PATH_ROOT_PROJECT, .sub = "H",   .expect = "" },
  { .name = "root_is_its_own_parent",          .root = SPN_PATH_ROOT_PROJECT, .sub = "",    .expect = "" },
  { .name = "absolute_drops_the_leaf",         .sub = "/A/H", .expect = "/A" },
  { .name = "absolute_top_level_is_the_slash", .sub = "/H",   .expect = "/" },
  { .name = "slash_is_its_own_parent",         .sub = "/",    .expect = "/" },
  { .name = "relative_top_level_is_empty",     .sub = "H",    .expect = "" },
};

sp_test_each(paths_path, parent, parent_test_t, parent_tests) {
  spn_path_t parent = spn_path_parent((spn_path_t) { .root = it->root, .sub = sp_cstr_as_str(it->sub) });
  sp_expect_eq(t, parent.root, it->root);
  sp_expect_str_eq_c(t, parent.sub, it->expect);
  return SP_OK;
}
