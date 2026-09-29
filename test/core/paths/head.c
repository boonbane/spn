#include "paths/paths_test.h"

#define HEAD_MAX_SETUP 8

typedef enum {
  HEAD_SETUP_FILE,
  HEAD_SETUP_DIR,
  HEAD_SETUP_SYMLINK,
} head_setup_kind_t;

typedef struct {
  const c8* path;
  head_setup_kind_t kind;
  const c8* target;
} head_setup_t;

typedef struct {
  bool set;
  const c8* sub;
  bool exists;
} head_expect_t;

typedef struct {
  const c8* name;
  head_setup_t setup [HEAD_MAX_SETUP];
  bool symlinks;
  const c8* input;
  head_expect_t expect;
  head_expect_t windows;
} head_test_t;

static const head_test_t head_tests [] = {
  {
    .name = "root_itself",
    .input = "",
    .expect = { .sub = "", .exists = true },
  },
  {
    .name = "existing_file",
    .setup = {
      { "A", HEAD_SETUP_DIR },
      { "A/B" },
    },
    .input = "A/B",
    .expect = { .sub = "A/B", .exists = true },
  },
  {
    .name = "existing_dotdot_resolves",
    .setup = {
      { "A", HEAD_SETUP_DIR },
      { "A/B", HEAD_SETUP_DIR },
    },
    .input = "A/B/..",
    .expect = { .sub = "A", .exists = true },
  },
  {
    .name = "missing_leaf",
    .setup = {
      { "A", HEAD_SETUP_DIR },
    },
    .input = "A/M",
    .expect = { .sub = "A/M" },
  },
  {
    .name = "missing_component_drops_tail",
    .setup = {
      { "A", HEAD_SETUP_DIR },
    },
    .input = "A/M/D/E",
    .expect = { .sub = "A/M" },
  },
  {
    .name = "dotdot_after_missing_is_not_collapsed",
    .setup = {
      { "A", HEAD_SETUP_DIR },
      { "A/X" },
    },
    .input = "A/M/../X",
    .expect = { .sub = "A/M" },
    .windows = { .set = true, .sub = "A/X", .exists = true },
  },
  {
    .name = "dotdot_over_existing_dir",
    .setup = {
      { "A", HEAD_SETUP_DIR },
      { "A/B", HEAD_SETUP_DIR },
    },
    .input = "A/B/../M",
    .expect = { .sub = "A/M" },
  },
  {
    .name = "dot_segment",
    .setup = {
      { "A", HEAD_SETUP_DIR },
    },
    .input = "A/./M",
    .expect = { .sub = "A/M" },
  },
  {
    .name = "dotdot_chain_climbs_to_root",
    .setup = {
      { "A", HEAD_SETUP_DIR },
      { "A/B", HEAD_SETUP_DIR },
    },
    .input = "A/B/../../M",
    .expect = { .sub = "M" },
  },
  {
    .name = "file_in_the_middle",
    .setup = {
      { "F" },
    },
    .input = "F/M",
    .expect = { .sub = "F/M" },
  },
  {
    .name = "trailing_dot_on_file_degrades_to_prefix",
    .setup = {
      { "F" },
    },
    .input = "F/.",
    .expect = { .sub = "F", .exists = true },
  },
  {
    .name = "dotdot_through_symlink_is_physical",
    .setup = {
      { "T", HEAD_SETUP_DIR },
      { "T/S", HEAD_SETUP_DIR },
      { .path = "L", .kind = HEAD_SETUP_SYMLINK, .target = "T/S" },
    },
    .symlinks = true,
    .input = "L/../M",
    .expect = { .sub = "T/M" },
    .windows = { .set = true, .sub = "M" },
  },
  {
    .name = "symlink_resolves",
    .setup = {
      { "A" },
      { .path = "L", .kind = HEAD_SETUP_SYMLINK, .target = "A" },
    },
    .symlinks = true,
    .input = "L",
    .expect = { .sub = "A", .exists = true },
  },
  {
    .name = "missing_under_symlink",
    .setup = {
      { "T", HEAD_SETUP_DIR },
      { .path = "L", .kind = HEAD_SETUP_SYMLINK, .target = "T" },
    },
    .symlinks = true,
    .input = "L/M",
    .expect = { .sub = "T/M" },
  },
};

sp_test_each(paths_canonicalize_head, probe, head_test_t, head_tests) {
  if (it->symlinks) {
    sp_test_skip_without_symlinks();
  }

  sp_mem_t mem = sp_test_arena(t);
  sp_path_t sandbox = sp_test_dir(t);

  u32 count = 0;
  sp_carr_detect_len(it->setup, count, it->setup[count].path);
  sp_for(at, count) {
    const head_setup_t* s = &it->setup[at];
    sp_path_t path = sp_path_join(mem, sandbox, sp_str_view(s->path));
    switch (s->kind) {
      case HEAD_SETUP_FILE: {
        sp_must_ok(t, sp_fs_create_file_at(path));
        break;
      }
      case HEAD_SETUP_DIR: {
        sp_must_ok(t, sp_fs_create_dir_at(path));
        break;
      }
      case HEAD_SETUP_SYMLINK: {
        sp_path_t target = sp_path_join(mem, sandbox, sp_str_view(s->target));
        sp_fs_kind_t kind = sp_fs_is_target_dir_at(target) ? SP_FS_KIND_DIR : SP_FS_KIND_FILE;
        sp_must_ok(t, sp_fs_create_sym_link_at(sp_str_view(s->target), path, kind));
        break;
      }
    }
  }

  const head_expect_t* expect = &it->expect;
#if defined(SP_WIN32)
  if (it->windows.set) {
    expect = &it->windows;
  }
#endif

  spn_path_roots_t roots = sp_zero;
  spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_PROJECT, sandbox);

  spn_path_t input = { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_str_view(it->input) };
  spn_path_t result = spn_path_canonicalize_head(mem, &roots, input);
  sp_expect_eq(t, result.root, SPN_PATH_ROOT_PROJECT);
  sp_expect_str_eq_c(t, result.sub, expect->sub);
  sp_expect_eq(t, sp_fs_exists_at(spn_path_at(&roots, result)), expect->exists);

  spn_path_t again = spn_path_canonicalize_head(mem, &roots, result);
  sp_expect(t, spn_path_equal(again, result));
  spn_path_roots_close(&roots);
  return SP_OK;
}
