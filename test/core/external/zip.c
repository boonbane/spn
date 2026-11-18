#include "unit.h"
#include "fixture.h"

#include "external/zip.h"

#define zip_test_max_entries 2

typedef struct {
  spn_zip_kind_t kind;
  const c8* path;
  const c8* link;
  const c8* content;
  bool executable;
  sp_err_t err;
} entry_t;

typedef struct {
  entry_t entries [zip_test_max_entries];
  sp_err_t err;
} expect_t;

typedef struct {
  const c8* name;
  const c8* archive;
  u32 flip;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "stored_entry_reads_back",
    .archive = "stored",
    .expect = { .entries = { { .path = "B", .content = "B" } } },
  },
  {
    .name = "deflated_entry_inflates",
    .archive = "deflated",
    .expect = { .entries = { { .path = "Z", .content = "BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB" } } },
  },
  {
    .name = "other_compression_is_unsupported",
    .archive = "bzip2",
    .expect = { .err = SP_ERR_SYS_UNSUPPORTED },
  },
  {
    .name = "directory_entry_is_a_directory",
    .archive = "dir",
    .expect = { .entries = { { .kind = SPN_ZIP_DIR, .path = "A/" } } },
  },
  {
    .name = "executable_bit_is_reported",
    .archive = "executable",
    .expect = { .entries = { { .path = "C", .content = "C", .executable = true } } },
  },
  {
    .name = "symlink_reports_its_target",
    .archive = "symlink",
    .expect = { .entries = { { .kind = SPN_ZIP_SYM_LINK, .path = "D", .link = "B" } } },
  },
  {
    .name = "entries_arrive_in_archive_order",
    .archive = "entries",
    .expect = {
      .entries = {
        { .path = "B", .content = "B" },
        { .path = "E", .content = "E" },
      },
    },
  },
  {
    .name = "name_longer_than_the_buffer_is_an_error",
    .archive = "name",
    .expect = { .err = SP_ERR_IO_NO_SPACE },
  },
  {
    .name = "content_failing_its_checksum_is_an_error",
    .archive = "stored",
    .flip = 31,
    .expect = { .entries = { { .path = "B", .err = SP_ERR } } },
  },
  {
    .name = "link_target_failing_its_checksum_is_an_error",
    .archive = "symlink",
    .flip = 31,
    .expect = { .err = SP_ERR },
  },
  {
    .name = "header_contradicting_the_directory_is_an_error",
    .archive = "stored",
    .flip = 8,
    .expect = { .err = SP_ERR },
  },
  {
    .name = "damaged_directory_record_is_an_error",
    .archive = "stored",
    .flip = 32,
    .expect = { .err = SP_ERR },
  },
  {
    .name = "entry_from_a_non_unix_host_is_a_file",
    .archive = "symlink",
    .flip = 37,
    .expect = { .entries = { { .path = "D", .content = "B" } } },
  },
};

sp_test_each(zip, read, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t archive = sp_zero;
  sp_must_ok(t, sp_fs_read(mem, test_repo_path(mem, sp_test_format(t, "test/core/external/zip/{}.zip", sp_fmt_cstr(it->archive))), &archive));
  if (it->flip) {
    ((c8*)archive.data)[it->flip] ^= 0x7f;
  }

  sp_path_t path = sp_path_join(mem, sp_test_dir(t), sp_str_lit("A"));
  sp_must_ok(t, sp_fs_write_at(path, archive, SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms));
  sp_io_file_reader_t file = sp_zero;
  sp_must_ok(t, sp_io_file_reader_from_path_at(&file, path));

  spn_zip_it_t zip = sp_zero;
  sp_must_eq(t, (u32)SPN_OK, (u32)spn_zip_it_init(&zip, &file));

  u32 count = 0;
  sp_carr_detect_len(it->expect.entries, count, it->expect.entries[count].path);

  spn_zip_entry_t entry = sp_zero;
  u8 names [16] = sp_zero;
  u32 index = 0;
  while (spn_zip_it_next(&zip, sp_mem_slice(names, sizeof(names)), &entry)) {
    sp_must_lt(t, index, count);
    const entry_t* expect = &it->expect.entries[index];
    sp_expect_eq(t, expect->kind, entry.kind);
    sp_expect_str_eq_c(t, entry.path, expect->path);
    sp_expect_str_eq_c(t, entry.link, expect->link);
    sp_expect_eq(t, expect->executable, entry.executable);

    if (entry.kind == SPN_ZIP_FILE) {
      c8 content [128] = sp_zero;
      u64 size = 0;
      sp_err_t err = sp_io_read_all(&zip.content, content, sizeof(content), &size);
      if (err == SP_ERR_IO_EOF) {
        err = SP_OK;
      }
      sp_expect_err_eq(t, expect->err, err);
      if (!err) {
        sp_expect_str_eq_c(t, sp_str(content, (u32)size), expect->content);
        sp_expect_eq(t, size, entry.size);
      }
    }
    index++;
  }
  sp_expect_eq(t, count, index);
  sp_expect_err_eq(t, it->expect.err, zip.err);

  spn_zip_it_close(&zip);
  sp_io_file_reader_close(&file);
  return SP_OK;
}
