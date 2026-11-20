#include "entries.h"

typedef struct {
  pin_t indexes [LOCK_TEST_MAX_INDEXES];
  entry_t entries [LOCK_TEST_MAX_ENTRIES];
} state_t;

typedef struct {
  bool changed;
  const c8* what;
} expect_t;

typedef struct {
  const c8* name;
  state_t held;
  state_t next;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "same",
    .held = {
      .indexes = { { .name = "I", .url = "U", .rev = "R" } },
      .entries = { { .name = "A", .version = { 1, 0, 0 } } },
    },
    .next = {
      .indexes = { { .name = "I", .url = "U", .rev = "R" } },
      .entries = { { .name = "A", .version = { 1, 0, 0 } } },
    },
  },
  {
    .name = "added_pin",
    .next.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .expect = { .changed = true, .what = "I" },
  },
  {
    .name = "removed_pin",
    .held.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .expect = { .changed = true, .what = "I" },
  },
  {
    .name = "moved_pin",
    .held.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .next.indexes = { { .name = "I", .url = "U", .rev = "S" } },
    .expect = { .changed = true, .what = "I" },
  },
  {
    .name = "rehosted_pin",
    .held.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .next.indexes = { { .name = "I", .url = "V", .rev = "R" } },
    .expect = { .changed = true, .what = "I" },
  },
  {
    .name = "added_entry",
    .next.entries = { { .name = "A", .version = { 1, 0, 0 } } },
    .expect = { .changed = true, .what = "A" },
  },
  {
    .name = "removed_entry",
    .held.entries = { { .name = "A", .version = { 1, 0, 0 } } },
    .expect = { .changed = true, .what = "A" },
  },
  {
    .name = "pin_is_named_before_an_entry",
    .held.entries = { { .name = "A", .version = { 1, 0, 0 } } },
    .next.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .expect = { .changed = true, .what = "I" },
  },
};

static spn_lock_t lock_from(sp_mem_t mem, const state_t* state) {
  u32 entries = 0;
  sp_carr_detect_len(state->entries, entries, state->entries[entries].name);
  u32 indexes = 0;
  sp_carr_detect_len(state->indexes, indexes, state->indexes[indexes].name);

  spn_lock_t lock = lock_of(mem, state->entries, entries);
  pin_all(&lock, state->indexes, indexes);
  return lock;
}

sp_test_each(lock, changed, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_lock_t held = lock_from(mem, &it->held);
  spn_lock_t next = lock_from(mem, &it->next);

  sp_str_t what = sp_zero;
  sp_expect_eq(t, it->expect.changed, si_lock_changed(&held, &next, &what));
  sp_expect_str_eq(t, what, text(it->expect.what));
  return SP_OK;
}
