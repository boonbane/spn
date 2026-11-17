#include "unit.h"
#include "fixture.h"

#include "external/gzip.h"

typedef struct {
  const c8* content;
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
    .name = "member_decodes",
    .archive = "member",
    .expect = { .content = "A" },
  },
  {
    .name = "concatenated_members_decode_as_one",
    .archive = "members",
    .expect = { .content = "AB" },
  },
  {
    .name = "truncated_member_is_an_error",
    .archive = "member",
    .keep = 12,
    .expect = { .err = SP_ERR },
  },
  {
    .name = "corrupt_member_is_an_error",
    .archive = "member",
    .flip = 13,
    .expect = { .err = SP_ERR },
  },
};

sp_test_each(gzip, read, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t archive = sp_zero;
  sp_must_ok(t, sp_fs_read(mem, test_repo_path(mem, sp_test_format(t, "test/core/external/gzip/{}.gz", sp_fmt_cstr(it->archive))), &archive));
  if (it->keep) {
    archive.len = it->keep;
  }
  if (it->flip) {
    ((c8*)archive.data)[it->flip] ^= 0x7f;
  }

  sp_io_reader_t compressed = sp_zero;
  sp_io_reader_from_mem(&compressed, archive.data, archive.len);
  spn_gzip_reader_t gzip = sp_zero;
  sp_must_eq(t, (u32)SPN_OK, (u32)spn_gzip_reader_init(&gzip, mem, &compressed));

  c8 content [8] = sp_zero;
  u64 size = 0;
  sp_err_t err = sp_io_read_all(&gzip.base, content, sizeof(content), &size);
  spn_gzip_reader_close(&gzip);

  if (err == SP_ERR_IO_EOF) {
    err = SP_OK;
  }
  sp_expect_err_eq(t, it->expect.err, err);
  if (!err) {
    sp_expect_str_eq_c(t, sp_str(content, (u32)size), it->expect.content);
  }
  return SP_OK;
}
