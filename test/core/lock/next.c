#include "entries.h"

#define NEXT_TEST_MAX_MOVES 2

typedef struct {
  pin_t indexes [LOCK_TEST_MAX_INDEXES];
  entry_t entries [LOCK_TEST_MAX_ENTRIES];
} state_t;

typedef struct {
  entry_t entry;
  bool pinned;
} pick_t;

typedef struct {
  state_t lock;
  const c8* moved [NEXT_TEST_MAX_MOVES];
  bool changed;
  const c8* stale;
} expect_t;

typedef struct {
  const c8* name;
  bool absent;
  state_t held;
  state_t kept;
  pick_t picks [LOCK_TEST_MAX_ENTRIES];
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "pick_is_added",
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } } },
    .expect = {
      .lock.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
      .changed = true,
      .stale = "A",
    },
  },
  {
    .name = "same_pick_changes_nothing",
    .held.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .kept.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } } },
    .expect.lock.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
  },
  {
    .name = "unpicked_entry_stays",
    .held.entries = { { .name = "A", .version = { 1, 0, 0 } } },
    .kept.entries = { { .name = "A", .version = { 1, 0, 0 } } },
    .expect.lock.entries = { { .name = "A", .version = { 1, 0, 0 } } },
  },
  {
    .name = "dropped_entry_is_stale",
    .held.entries = {
      { .name = "A", .version = { 1, 0, 0 } },
      { .name = "B", .version = { 1, 0, 0 } },
    },
    .kept.entries = { { .name = "B", .version = { 1, 0, 0 } } },
    .expect = {
      .lock.entries = { { .name = "B", .version = { 1, 0, 0 } } },
      .changed = true,
      .stale = "A",
    },
  },
  {
    .name = "new_version_joins_the_old",
    .held.entries = { { .name = "A", .version = { 1, 0, 0 } } },
    .kept.entries = { { .name = "A", .version = { 1, 0, 0 } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 1, 0 } } } },
    .expect = {
      .lock.entries = {
        { .name = "A", .version = { 1, 0, 0 } },
        { .name = "A", .version = { 1, 1, 0 } },
      },
      .changed = true,
      .stale = "A",
    },
  },
  {
    .name = "moved_source_is_reported",
    .held.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .kept.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } } } },
    .expect = {
      .lock.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
      .moved = { "A" },
    },
  },
  {
    .name = "moved_manifest_is_reported",
    .held.entries = { { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "R" } } },
    .kept.entries = { { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "R" } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "S" } } } },
    .expect = {
      .lock.entries = { { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "R" } } },
      .moved = { "A" },
    },
  },
  {
    .name = "moves_are_reported_in_order",
    .held.entries = {
      { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
      { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "V", .rev = "R" } },
    },
    .kept.entries = {
      { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
      { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "V", .rev = "R" } },
    },
    .picks = {
      { .entry = { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "V", .rev = "S" } } },
      { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } } },
    },
    .expect = {
      .lock.entries = {
        { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
        { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "V", .rev = "R" } },
      },
      .moved = { "A", "B" },
    },
  },
  {
    .name = "pinned_pick_replaces_the_entry",
    .held.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .kept.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } }, .pinned = true } },
    .expect = {
      .lock.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } } },
      .changed = true,
      .stale = "A",
    },
  },
  {
    .name = "kept_pin_is_carried",
    .held.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .kept.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .expect.lock.indexes = { { .name = "I", .url = "U", .rev = "R" } },
  },
  {
    .name = "added_pin_is_stale",
    .kept.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .expect = {
      .lock.indexes = { { .name = "I", .url = "U", .rev = "R" } },
      .changed = true,
      .stale = "I",
    },
  },
  {
    .name = "removed_pin_is_stale",
    .held.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .expect = {
      .changed = true,
      .stale = "I",
    },
  },
  {
    .name = "moved_pin_is_stale",
    .held.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .kept.indexes = { { .name = "I", .url = "U", .rev = "S" } },
    .expect = {
      .lock.indexes = { { .name = "I", .url = "U", .rev = "S" } },
      .changed = true,
      .stale = "I",
    },
  },
  {
    .name = "rehosted_pin_is_stale",
    .held.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .kept.indexes = { { .name = "I", .url = "V", .rev = "R" } },
    .expect = {
      .lock.indexes = { { .name = "I", .url = "V", .rev = "R" } },
      .changed = true,
      .stale = "I",
    },
  },
  {
    .name = "stale_pin_is_named_before_a_stale_entry",
    .held.entries = { { .name = "A", .version = { 1, 0, 0 } } },
    .kept.indexes = { { .name = "I", .url = "U", .rev = "R" } },
    .expect = {
      .lock.indexes = { { .name = "I", .url = "U", .rev = "R" } },
      .changed = true,
      .stale = "I",
    },
  },
  {
    .name = "absent_lock_is_changed",
    .absent = true,
    .expect.changed = true,
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

sp_test_each(lock, next, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_lock_t held = lock_from(mem, &it->held);
  if (it->absent) {
    held.version = 0;
  }
  spn_lock_t kept = lock_from(mem, &it->kept);

  si_da(si_lock_pick_t) picks = sp_zero;
  u32 num_picks = 0;
  sp_carr_detect_len(it->picks, num_picks, it->picks[num_picks].entry.name);
  sp_for(at, num_picks) {
    si_da_push(mem, picks, ((si_lock_pick_t) { .entry = entry(it->picks[at].entry), .pinned = it->picks[at].pinned }));
  }

  si_lock_next_t next = si_lock_next(mem, (si_lock_request_t) { .held = &held, .kept = &kept, .picks = picks });

  u32 indexes = 0;
  sp_carr_detect_len(it->expect.lock.indexes, indexes, it->expect.lock.indexes[indexes].name);
  sp_try(expect_pins(t, &next.lock, it->expect.lock.indexes, indexes));
  u32 entries = 0;
  sp_carr_detect_len(it->expect.lock.entries, entries, it->expect.lock.entries[entries].name);
  sp_try(expect_entries(t, &next.lock, it->expect.lock.entries, entries));

  u32 moved = 0;
  sp_carr_detect_len(it->expect.moved, moved, it->expect.moved[moved]);
  sp_must_eq(t, moved, (u32)si_da_size(next.moved));
  sp_for(at, moved) {
    sp_expect_str_eq_c(t, next.moved[at].was.name.str, it->expect.moved[at]);
  }

  sp_expect_eq(t, it->expect.changed, next.changed);
  sp_expect_str_eq(t, next.stale, text(it->expect.stale));
  return SP_OK;
}
