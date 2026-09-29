#include "spn_test.h"
#include "core/core.h"
#include "fs/fs.h"

#define COPY_TEST_MAX_ENTRIES 8

typedef enum {
  OP_UPDATE_FILE,
  OP_UPDATE_GLOB,
} op_t;

typedef struct {
  const c8* path;
  const c8* content;
} file_t;

typedef struct {
  const c8* path;
  const c8* target;
} link_t;

typedef struct {
  file_t files [COPY_TEST_MAX_ENTRIES];
  const c8* dirs [COPY_TEST_MAX_ENTRIES];
  link_t symlinks [COPY_TEST_MAX_ENTRIES];
  link_t hardlinks [COPY_TEST_MAX_ENTRIES];
} setup_t;

typedef struct {
  bool err;
  bool untouched;
  file_t files [COPY_TEST_MAX_ENTRIES];
  const c8* dirs [COPY_TEST_MAX_ENTRIES];
  const c8* absent [COPY_TEST_MAX_ENTRIES];
} expect_t;

typedef struct {
  const c8* name;
  op_t op;
  setup_t setup;
  const c8* from;
  const c8* to;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "file_copies",
    .setup.files = { { "A", "X" } },
    .from = "A",
    .to = "B",
    .expect.files = { { "B", "X" } },
  },
  {
    .name = "file_creates_parents",
    .setup.files = { { "A", "X" } },
    .from = "A",
    .to = "D/E/B",
    .expect.files = { { "D/E/B", "X" } },
  },
  {
    .name = "file_onto_dir",
    .setup.files = { { "A", "X" } },
    .setup.dirs = { "D" },
    .from = "A",
    .to = "D",
    .expect = { .err = true, .dirs = { "D" }, .absent = { "D/A" } },
  },
  {
    .name = "file_identical_untouched",
    .setup.files = { { "A", "X" }, { "B", "X" } },
    .from = "A",
    .to = "B",
    .expect.untouched = true,
    .expect.files = { { "B", "X" } },
  },
  {
    .name = "file_leaves_hardlinks_alone",
    .setup.files = { { "A", "X" }, { "B", "Y" } },
    .setup.hardlinks = { { "C", "B" } },
    .from = "A",
    .to = "B",
    .expect.files = { { "B", "X" }, { "C", "Y" } },
  },
  {
    .name = "file_missing_source",
    .from = "Z",
    .to = "B",
    .expect.err = true,
    .expect.absent = { "B" },
  },
  {
    .name = "glob_star_copies_tree",
    .op = OP_UPDATE_GLOB,
    .setup.files = { { "A/X", "1" }, { "A/B/Y", "2" } },
    .setup.dirs = { "A/C" },
    .from = "A/*",
    .to = "D",
    .expect.files = { { "D/X", "1" }, { "D/B/Y", "2" } },
    .expect.dirs = { "D/C" },
  },
  {
    .name = "glob_pattern_filters",
    .op = OP_UPDATE_GLOB,
    .setup.files = { { "A/X.C", "1" }, { "A/Y.H", "2" } },
    .from = "A/*.C",
    .to = "D",
    .expect.files = { { "D/X.C", "1" } },
    .expect.absent = { "D/Y.H" },
  },
  {
    .name = "glob_pattern_copies_dir",
    .op = OP_UPDATE_GLOB,
    .setup.files = { { "A/B.C/Z", "1" } },
    .from = "A/*.C",
    .to = "D",
    .expect.files = { { "D/B.C/Z", "1" } },
  },
  {
    .name = "glob_follows_symlink",
    .op = OP_UPDATE_GLOB,
    .setup.files = { { "A/X", "1" } },
    .setup.symlinks = { { "A/L", "A/X" } },
    .from = "A/*",
    .to = "D",
    .expect.files = { { "D/X", "1" }, { "D/L", "1" } },
  },
  {
    .name = "glob_dangling_symlink_fails",
    .op = OP_UPDATE_GLOB,
    .setup.dirs = { "A" },
    .setup.symlinks = { { "A/L", "A/Z" } },
    .from = "A/*",
    .to = "D",
    .expect.err = true,
  },
  {
    .name = "glob_no_match_creates_dest",
    .op = OP_UPDATE_GLOB,
    .setup.files = { { "A/X.C", "1" } },
    .from = "A/*.H",
    .to = "D",
    .expect.dirs = { "D" },
    .expect.absent = { "D/X.C" },
  },
  {
    .name = "glob_dest_is_file_fails",
    .op = OP_UPDATE_GLOB,
    .setup.files = { { "A/X.C", "1" }, { "D", "2" } },
    .from = "A/*.H",
    .to = "D",
    .expect.err = true,
    .expect.files = { { "D", "2" } },
  },
  {
    .name = "glob_missing_dir_fails",
    .op = OP_UPDATE_GLOB,
    .from = "Z/*",
    .to = "D",
    .expect.err = true,
    .expect.absent = { "D" },
  },
};

sp_test_each(fs_update, cases, test_t, tests) {
  if (it->setup.symlinks[0].path) {
    sp_test_skip_without_symlinks();
  }

  sp_mem_t mem = sp_test_arena(t);
  sp_path_t root = sp_test_dir(t);
  sp_str_t absolute = sp_fs_canonicalize_path_at(mem, root);

  u32 dirs = 0;
  sp_carr_detect_len(it->setup.dirs, dirs, it->setup.dirs[dirs]);
  sp_for(i, dirs) {
    sp_must_ok(t, sp_fs_create_dir_at(sp_path_join(mem, root, sp_cstr_as_str(it->setup.dirs[i]))));
  }

  u32 files = 0;
  sp_carr_detect_len(it->setup.files, files, it->setup.files[files].path);
  sp_for(i, files) {
    file_t file = it->setup.files[i];
    sp_path_t path = sp_path_join(mem, root, sp_cstr_as_str(file.path));
    sp_must_ok(t, sp_fs_create_parent(path));
    sp_must_ok(t, sp_fs_create_file_cstr_at(path, file.content));
  }

  u32 symlinks = 0;
  sp_carr_detect_len(it->setup.symlinks, symlinks, it->setup.symlinks[symlinks].path);
  sp_for(i, symlinks) {
    link_t link = it->setup.symlinks[i];
    sp_str_t target = sp_fs_join_path(mem, absolute, sp_cstr_as_str(link.target));
    sp_fs_kind_t kind = sp_fs_is_target_dir(target) ? SP_FS_KIND_DIR : SP_FS_KIND_FILE;
    sp_must_ok(t, sp_fs_create_sym_link_at(target, sp_path_join(mem, root, sp_cstr_as_str(link.path)), kind));
  }

  u32 hardlinks = 0;
  sp_carr_detect_len(it->setup.hardlinks, hardlinks, it->setup.hardlinks[hardlinks].path);
  sp_for(i, hardlinks) {
    link_t link = it->setup.hardlinks[i];
    sp_must_ok(t, sp_fs_create_hard_link_at(sp_path_join(mem, root, sp_cstr_as_str(link.target)), sp_path_join(mem, root, sp_cstr_as_str(link.path))));
  }

  sp_path_t from = sp_path_join(mem, root, sp_cstr_as_str(it->from));
  sp_path_t to = sp_path_join(mem, root, sp_cstr_as_str(it->to));

  sp_sys_file_meta_t before = sp_zero;
  sp_sys_get_path_metadata_s(to.dir, to.sub, &before);

  bool failed = false;
  switch (it->op) {
    case OP_UPDATE_FILE: {
      failed = spn_fs_update_file(from, to) != SPN_OK;
      break;
    }
    case OP_UPDATE_GLOB: {
      failed = spn_fs_update_glob(from, to) != SPN_OK;
      break;
    }
  }
  sp_expect_eq(t, it->expect.err, failed);

  u32 expected = 0;
  sp_carr_detect_len(it->expect.files, expected, it->expect.files[expected].path);
  sp_for(i, expected) {
    file_t file = it->expect.files[i];
    sp_str_t content = sp_zero;
    sp_must_ok(t, sp_io_read_file_at(mem, sp_path_join(mem, root, sp_cstr_as_str(file.path)), &content));
    sp_expect_str_eq_c(t, content, file.content);
  }

  u32 expected_dirs = 0;
  sp_carr_detect_len(it->expect.dirs, expected_dirs, it->expect.dirs[expected_dirs]);
  sp_for(i, expected_dirs) {
    sp_expect(t, sp_fs_is_dir_at(sp_path_join(mem, root, sp_cstr_as_str(it->expect.dirs[i]))));
  }

  u32 absent = 0;
  sp_carr_detect_len(it->expect.absent, absent, it->expect.absent[absent]);
  sp_for(i, absent) {
    sp_expect(t, !sp_fs_exists_at(sp_path_join(mem, root, sp_cstr_as_str(it->expect.absent[i]))));
  }

  if (it->expect.untouched) {
    sp_sys_file_meta_t after = sp_zero;
    sp_must_ok(t, sp_sys_get_path_metadata_s(to.dir, to.sub, &after));
    sp_expect_eq(t, before.id, after.id);
  }

  return SP_OK;
}

typedef struct {
  const c8* name;
  file_t existing;
  const c8* path;
  const c8* content;
  bool untouched;
} str_test_t;

static const str_test_t str_tests [] = {
  { .name = "creates_parents",     .path = "D/E/F", .content = "A" },
  { .name = "identical_untouched", .existing = { "F", "A" }, .path = "F", .content = "A", .untouched = true },
  { .name = "different_replaced",  .existing = { "F", "A" }, .path = "F", .content = "B" },
};

sp_test_each(fs_update, str, str_test_t, str_tests) {
  sp_mem_t mem = sp_test_arena(t);
  sp_path_t root = sp_test_dir(t);
  sp_path_t path = sp_path_join(mem, root, sp_cstr_as_str(it->path));

  sp_sys_file_meta_t before = sp_zero;
  if (it->existing.path) {
    sp_must_ok(t, sp_fs_create_file_cstr_at(sp_path_join(mem, root, sp_cstr_as_str(it->existing.path)), it->existing.content));
    sp_must_ok(t, sp_sys_get_path_metadata_s(path.dir, path.sub, &before));
  }

  sp_must_eq(t, (u32)SPN_OK, (u32)spn_fs_update_file_str(path, sp_cstr_as_str(it->content)));

  sp_str_t content = sp_zero;
  sp_must_ok(t, sp_io_read_file_at(mem, path, &content));
  sp_expect_str_eq_c(t, content, it->content);

  if (it->untouched) {
    sp_sys_file_meta_t after = sp_zero;
    sp_must_ok(t, sp_sys_get_path_metadata_s(path.dir, path.sub, &after));
    sp_expect_eq(t, before.id, after.id);
  }
  return SP_OK;
}
