#include "entries.h"

typedef struct {
  const c8* name;
  spn_semver_t version;
  bool was;
  bool now;
} change_t;

typedef struct {
  const c8* name;
  entry_t held [LOCK_TEST_MAX_ENTRIES];
  entry_t next [LOCK_TEST_MAX_ENTRIES];
  struct {
    change_t changes [LOCK_TEST_MAX_ENTRIES];
  } expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "same",
    .held = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .next = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
  },
  {
    .name = "added",
    .next = { { .name = "A", .version = { 1, 0, 0 } } },
    .expect.changes = { { .name = "A", .version = { 1, 0, 0 }, .now = true } },
  },
  {
    .name = "removed",
    .held = { { .name = "A", .version = { 1, 0, 0 } } },
    .expect.changes = { { .name = "A", .version = { 1, 0, 0 }, .was = true } },
  },
  {
    .name = "source_moved",
    .held = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .next = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } } },
    .expect.changes = { { .name = "A", .version = { 1, 0, 0 }, .was = true, .now = true } },
  },
  {
    .name = "manifest_moved",
    .held = { { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "R", .dir = "D" } } },
    .next = { { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "R", .dir = "E" } } },
    .expect.changes = { { .name = "A", .version = { 1, 0, 0 }, .was = true, .now = true } },
  },
  {
    .name = "version_moved",
    .held = { { .name = "A", .version = { 1, 0, 0 } } },
    .next = { { .name = "A", .version = { 1, 1, 0 } } },
    .expect.changes = {
      { .name = "A", .version = { 1, 0, 0 }, .was = true },
      { .name = "A", .version = { 1, 1, 0 }, .now = true },
    },
  },
  {
    .name = "interleaved",
    .held = {
      { .name = "A", .version = { 1, 0, 0 } },
      { .name = "C", .version = { 1, 0, 0 } },
    },
    .next = {
      { .name = "B", .version = { 1, 0, 0 } },
      { .name = "C", .version = { 1, 0, 0 } },
    },
    .expect.changes = {
      { .name = "A", .version = { 1, 0, 0 }, .was = true },
      { .name = "B", .version = { 1, 0, 0 }, .now = true },
    },
  },
};

sp_test_each(lock, diff, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  u32 num_held = 0;
  sp_carr_detect_len(it->held, num_held, it->held[num_held].name);
  u32 num_next = 0;
  sp_carr_detect_len(it->next, num_next, it->next[num_next].name);
  spn_lock_t held = lock_of(mem, it->held, num_held);
  spn_lock_t next = lock_of(mem, it->next, num_next);

  u32 len = 0;
  sp_carr_detect_len(it->expect.changes, len, it->expect.changes[len].name);

  u32 seen = 0;
  spn_lock_diff_t diff = spn_lock_diff(&held, &next);
  while (spn_lock_diff_next(&diff)) {
    sp_must(t, seen < len);
    const change_t* expected = &it->expect.changes[seen++];
    const spn_lock_entry_t* entry = diff.was ? diff.was : diff.now;
    sp_expect_str_eq_c(t, entry->name.str, expected->name);
    sp_expect(t, spn_semver_eq(entry->version, expected->version));
    sp_expect_eq(t, expected->was, diff.was != SP_NULLPTR);
    sp_expect_eq(t, expected->now, diff.now != SP_NULLPTR);
  }
  sp_expect_eq(t, len, seen);
  return SP_OK;
}
