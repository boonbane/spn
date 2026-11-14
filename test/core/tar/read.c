#include "unit.h"
#include "fixture.h"

#include "tar/tar.h"

#define tar_test_max_entries 8
#define tar_test_long "GGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGGG/F"

typedef struct {
  sp_tar_kind_t kind;
  const c8* path;
  const c8* link;
  const c8* content;
  u32 mode;
} entry_t;

typedef struct {
  entry_t entries [tar_test_max_entries];
  sp_err_t err;
} expect_t;

typedef struct {
  const c8* name;
  const c8* archive;
  u32 keep;
  u32 flip;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "kinds",
    .archive = "kinds",
    .expect = {
      .entries = {
        { .kind = SP_TAR_DIR, .path = "A", .mode = 0755 },
        { .path = "A/B", .content = "B", .mode = 0644 },
        { .path = "A/C", .content = "C", .mode = 0755 },
        { .kind = SP_TAR_SYM_LINK, .path = "A/D", .link = "B", .mode = 0777 },
        { .kind = SP_TAR_HARD_LINK, .path = "A/E", .link = "A/B", .mode = 0644 },
      },
    },
  },
  {
    .name = "ustar_prefix",
    .archive = "ustar",
    .expect = {
      .entries = {
        { .path = tar_test_long, .content = "F", .mode = 0644 },
      },
    },
  },
  {
    .name = "gnu_long_name",
    .archive = "gnu",
    .expect = {
      .entries = {
        { .path = tar_test_long, .content = "F", .mode = 0644 },
        { .kind = SP_TAR_SYM_LINK, .path = "D", .link = tar_test_long, .mode = 0777 },
      },
    },
  },
  {
    .name = "pax_path",
    .archive = "pax",
    .expect = {
      .entries = {
        { .path = tar_test_long, .content = "F", .mode = 0644 },
        { .kind = SP_TAR_SYM_LINK, .path = "D", .link = tar_test_long, .mode = 0777 },
      },
    },
  },
  {
    .name = "global_header",
    .archive = "git",
    .expect = {
      .entries = {
        { .kind = SP_TAR_DIR, .path = "A", .mode = 0755 },
        { .path = "A/B", .content = "B", .mode = 0644 },
      },
    },
  },
  {
    .name = "unsupported_kind",
    .archive = "fifo",
    .expect = {
      .err = SP_ERR_SYS_UNSUPPORTED,
    },
  },
  {
    .name = "truncated",
    .archive = "kinds",
    .keep = 1736,
    .expect = {
      .entries = {
        { .kind = SP_TAR_DIR, .path = "A", .mode = 0755 },
        { .path = "A/B", .content = "B", .mode = 0644 },
      },
      .err = SP_ERR_IO_EOF,
    },
  },
  {
    .name = "corrupt_header",
    .archive = "kinds",
    .flip = 1,
    .expect = {
      .err = SP_ERR,
    },
  },
};

sp_test_each(tar, read, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t archive = sp_zero;
  sp_must_ok(t, sp_fs_read(mem, test_repo_path(mem, sp_test_format(t, "test/core/tar/archives/{}.tar", sp_fmt_cstr(it->archive))), &archive));
  if (it->keep) {
    archive.len = it->keep;
  }
  if (it->flip) {
    ((c8*)archive.data)[it->flip] ^= 0x7f;
  }

  u32 count = 0;
  sp_carr_detect_len(it->expect.entries, count, it->expect.entries[count].path);

  sp_io_reader_t reader = sp_zero;
  sp_io_reader_from_mem(&reader, archive.data, archive.len);
  sp_tar_it_t tar = sp_tar_it(&reader);
  sp_tar_entry_t entry = sp_zero;
  u8 names [2 * SP_PATH_MAX] = sp_zero;
  u32 index = 0;
  while (sp_tar_it_next(&tar, sp_mem_slice(names, sizeof(names)), &entry)) {
    sp_must_lt(t, index, count);
    const entry_t* expect = &it->expect.entries[index];
    sp_expect_eq(t, expect->kind, entry.kind);
    sp_expect_str_eq_c(t, entry.path, expect->path);
    sp_expect_str_eq_c(t, entry.link, expect->link);
    sp_expect_eq(t, expect->mode, entry.mode);

    c8 content [8] = sp_zero;
    u64 size = 0;
    sp_io_read_all(&tar.content.base, content, sizeof(content), &size);
    sp_expect_str_eq_c(t, sp_str(content, (u32)size), expect->content);
    index++;
  }
  sp_expect_eq(t, count, index);
  sp_expect_err_eq(t, it->expect.err, tar.err);
  return SP_OK;
}
