#include "paths/paths_test.h"

typedef enum {
  SET_SETUP_NONE,
  SET_SETUP_ALIAS,
  SET_SETUP_FILE,
} set_setup_t;

typedef struct {
  bool err;
  const c8* dir;
} set_expect_t;

typedef struct {
  const c8* name;
  set_setup_t setup;
  const c8* path;
  set_expect_t expect;
} set_test_t;

static const set_test_t set_tests [] = {
  { .name = "missing_nested_dir_is_created",   .path = "A/B", .expect = { .dir = "A/B" } },
  { .name = "existing_dir_is_reused",          .path = "",    .expect = { .dir = "" } },
  { .name = "alias_stores_the_physical_dir",   .setup = SET_SETUP_ALIAS, .path = "L", .expect = { .dir = "A" } },
  { .name = "file_in_the_way_fails",           .setup = SET_SETUP_FILE,  .path = "F", .expect = { .err = true } },
};

sp_test_each(paths_set, root, set_test_t, set_tests) {
  sp_mem_t mem = sp_test_arena(t);
  sp_path_t sandbox = sp_test_dir(t);
  sp_str_t absolute = sp_fs_canonicalize_path_at(mem, sandbox);

  switch (it->setup) {
    case SET_SETUP_NONE: {
      break;
    }
    case SET_SETUP_ALIAS: {
      sp_test_skip_without_symlinks();
      sp_must_ok(t, sp_fs_create_dir_at(sp_path_join(mem, sandbox, sp_str_lit("A"))));
      sp_must_ok(t, sp_fs_create_sym_link_at(sp_str_lit("A"), sp_path_join(mem, sandbox, sp_str_lit("L")), SP_FS_KIND_DIR));
      break;
    }
    case SET_SETUP_FILE: {
      sp_must_ok(t, sp_fs_create_file_at(sp_path_join(mem, sandbox, sp_str_lit("F"))));
      break;
    }
  }

  spn_path_roots_t roots = sp_zero;
  spn_err_t err = spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_PROJECT, sp_path_join(mem, sandbox, sp_cstr_as_str(it->path)));
  sp_expect_eq(t, err != SPN_OK, it->expect.err);
  if (it->expect.err) {
    sp_expect_eq(t, roots.opened, 0u);
    return SP_OK;
  }

  sp_expect_eq(t, roots.opened, spn_path_root_mask(SPN_PATH_ROOT_PROJECT));
  sp_expect_str_eq(t, roots.dirs[SPN_PATH_ROOT_PROJECT], sp_fs_join_path(mem, absolute, sp_cstr_as_str(it->expect.dir)));
  sp_expect(t, sp_fs_is_dir_at(spn_path_at(&roots, spn_path_from_root(SPN_PATH_ROOT_PROJECT))));
  spn_path_roots_close(&roots);
  sp_expect_eq(t, roots.opened, 0u);
  return SP_OK;
}

sp_test(paths_set, unrooted_relative_resolves_from_cwd) {
  sp_mem_t mem = sp_test_arena(t);
  sp_path_t sandbox = sp_test_dir(t);
  sp_must_ok(t, sp_fs_create_file_at(sp_path_join(mem, sandbox, sp_str_lit("F"))));

  spn_path_roots_t roots = sp_zero;
  spn_path_t path = { .sub = sp_fs_join_path(mem, sandbox.sub, sp_str_lit("F")) };
  sp_expect(t, sp_fs_is_file_at(spn_path_at(&roots, path)));
  return SP_OK;
}
