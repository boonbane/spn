#include "spn_test.h"
#include "core/core.h"
#include "fs/fs.h"

#define COPY_TEST_MAX_ENTRIES 8

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
};

sp_test_each(fs_update, cases, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  sp_path_t root = sp_test_dir(t);

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
    sp_must_ok(t, sp_fs_create_parent_at(path));
    sp_must_ok(t, sp_fs_create_file_cstr_at(path, file.content));
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

  bool failed = spn_fs_update_file(from, to) != SPN_OK;
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
