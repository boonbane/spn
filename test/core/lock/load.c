#include "entries.h"

typedef struct {
  spn_err_t err;
  spn_err_t issue;
  u64 version;
  pin_t indexes [LOCK_TEST_MAX_INDEXES];
  entry_t entries [LOCK_TEST_MAX_ENTRIES];
} expect_t;

typedef struct {
  const c8* name;
  const c8* file;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "missing",
    .file = "locks/missing.lock",
  },
  {
    .name = "empty",
    .file = "golden/empty.lock",
    .expect.version = 1,
  },
  {
    .name = "unrooted",
    .file = "golden/unrooted.lock",
    .expect = {
      .version = 1,
      .entries = { { .name = "A", .version = { 1, 0, 0 } } },
    },
  },
  {
    .name = "split",
    .file = "golden/split.lock",
    .expect = {
      .version = 1,
      .entries = {
        {
          .name = "A",
          .version = { 1, 0, 0 },
          .source = { .url = "U", .rev = "R" },
          .manifest = { .url = "V", .rev = "S", .dir = "D" },
        },
      },
    },
  },
  {
    .name = "escapes",
    .file = "golden/escapes.lock",
    .expect = {
      .version = 1,
      .entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R", .dir = "D\\E\"F\nG" } } },
    },
  },
  {
    .name = "sorts",
    .file = "locks/unsorted.lock",
    .expect = {
      .version = 1,
      .entries = {
        { .name = "A", .version = { 1, 0, 0 } },
        { .name = "A", .version = { 2, 0, 0 } },
        { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
      },
    },
  },
  {
    .name = "indexed",
    .file = "golden/indexed.lock",
    .expect = {
      .version = 1,
      .indexes = { { .name = "I", .url = "U", .rev = "R" } },
    },
  },
  {
    .name = "index_duplicate",
    .file = "locks/duplicate_index.lock",
    .expect = { .err = SPN_ERR_LOCK_INVALID, .issue = SPN_ERR_CODEGEN_DUPLICATE_KEY },
  },
  {
    .name = "version_unsupported",
    .file = "locks/version.lock",
    .expect.err = SPN_ERR_LOCK_VERSION,
  },
  {
    .name = "version_missing",
    .file = "locks/unversioned.lock",
    .expect = { .err = SPN_ERR_LOCK_INVALID, .issue = SPN_ERR_CODEGEN_MISSING_KEY },
  },
  {
    .name = "package_version_invalid",
    .file = "locks/semver.lock",
    .expect = { .err = SPN_ERR_LOCK_INVALID, .issue = SPN_ERR_CODEGEN_INVALID },
  },
  {
    .name = "package_duplicate",
    .file = "locks/duplicate.lock",
    .expect = { .err = SPN_ERR_LOCK_INVALID, .issue = SPN_ERR_CODEGEN_DUPLICATE_KEY },
  },
  {
    .name = "unknown_key",
    .file = "locks/unknown.lock",
    .expect = { .err = SPN_ERR_LOCK_INVALID, .issue = SPN_ERR_CODEGEN_UNKNOWN_KEY },
  },
};

sp_test_each(lock, load, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_t path = { .sub = test_repo_path(mem, sp_fs_join_path(mem, sp_str_lit("test/core/lock"), sp_cstr_as_str(it->file))) };

  spn_lock_t lock = sp_zero;
  spn_err_union_t err = sp_zero;
  sp_must_eq(t, it->expect.err, spn_lock_load(mem, spn.intern, &spn.roots, path, &lock, &err));
  sp_expect_eq(t, it->expect.err, err.kind);
  if (it->expect.issue) {
    sp_must_eq(t, 1, sp_da_size(err.manifest.issues));
    sp_expect_eq(t, it->expect.issue, err.manifest.issues[0].code);
  }
  if (it->expect.err) {
    return SP_OK;
  }

  u32 len = 0;
  sp_carr_detect_len(it->expect.entries, len, it->expect.entries[len].name);
  sp_expect_eq(t, it->expect.version, lock.version);
  sp_try(expect_pins(t, &lock, it->expect.indexes));
  return expect_entries(t, &lock, it->expect.entries, len);
}
