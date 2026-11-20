#include "entries.h"

typedef struct {
  const c8* name;
  pin_t indexes [LOCK_TEST_MAX_INDEXES];
  entry_t entries [LOCK_TEST_MAX_ENTRIES];
} test_t;

static const test_t tests [] = {
  {
    .name = "empty",
  },
  {
    .name = "unrooted",
    .entries = {
      { .name = "A", .version = { 1, 0, 0 } },
    },
  },
  {
    .name = "source",
    .entries = {
      { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
    },
  },
  {
    .name = "split",
    .entries = {
      {
        .name = "A",
        .version = { 1, 0, 0 },
        .source = { .url = "U", .rev = "R" },
        .manifest = { .url = "V", .rev = "S", .dir = "D" },
      },
    },
  },
  {
    .name = "versions",
    .entries = {
      { .name = "B", .version = { 1, 0, 0 } },
      { .name = "A", .version = { 2, 0, 0 }, .source = { .url = "U", .rev = "S" } },
      { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
    },
  },
  {
    .name = "indexed",
    .indexes = { { .name = "I", .url = "U", .rev = "R" } },
  },
  {
    .name = "indexes",
    .indexes = {
      { .name = "J", .url = "V", .rev = "S" },
      { .name = "I", .url = "U", .rev = "R" },
    },
  },
  {
    .name = "escapes",
    .entries = {
      { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R", .dir = "D\\E\"F\nG" } },
    },
  },
};

sp_test_each(lock, save, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_t path = { .sub = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("spn.lock")) };

  u32 len = 0;
  sp_carr_detect_len(it->entries, len, it->entries[len].name);
  spn_lock_t lock = lock_of(mem, it->entries, len);
  u32 pins = 0;
  sp_carr_detect_len(it->indexes, pins, it->indexes[pins].name);
  pin_all(&lock, it->indexes, pins);

  spn_err_union_t err = sp_zero;
  sp_must_eq(t, SPN_OK, spn_lock_save(&spn.roots, path, &lock, &err));
  sp_expect_golden(t, test_repo_path(mem, sp_test_format(t, "test/core/lock/golden/{}.lock", sp_fmt_cstr(it->name))), test_read_file(mem, path.sub));
  return SP_OK;
}
