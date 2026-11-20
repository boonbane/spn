#include "entries.h"

#define MERGE_TEST_MAX_MOVES 2

typedef struct {
  entry_t entry;
  bool follows;
} pick_t;

typedef struct {
  entry_t was;
  entry_t now;
} move_t;

typedef struct {
  entry_t entries [LOCK_TEST_MAX_ENTRIES];
  move_t moved [MERGE_TEST_MAX_MOVES];
} expect_t;

typedef struct {
  const c8* name;
  entry_t kept [LOCK_TEST_MAX_ENTRIES];
  pick_t picks [LOCK_TEST_MAX_ENTRIES];
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "pick_is_added",
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } } },
    .expect.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
  },
  {
    .name = "same_pick_moves_nothing",
    .kept = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } } },
    .expect.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
  },
  {
    .name = "new_version_joins_the_old",
    .kept = { { .name = "A", .version = { 1, 0, 0 } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 1, 0 } } } },
    .expect.entries = {
      { .name = "A", .version = { 1, 0, 0 } },
      { .name = "A", .version = { 1, 1, 0 } },
    },
  },
  {
    .name = "moved_source_is_not_taken",
    .kept = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } } } },
    .expect = {
      .entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
      .moved = {
        {
          .was = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
          .now = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } },
        },
      },
    },
  },
  {
    .name = "moved_manifest_is_not_taken",
    .kept = { { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "R" } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "S" } } } },
    .expect = {
      .entries = { { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "R" } } },
      .moved = {
        {
          .was = { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "R" } },
          .now = { .name = "A", .version = { 1, 0, 0 }, .manifest = { .url = "U", .rev = "S" } },
        },
      },
    },
  },
  {
    .name = "moves_are_ordered",
    .kept = {
      { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
      { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "V", .rev = "R" } },
    },
    .picks = {
      { .entry = { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "V", .rev = "S" } } },
      { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } } },
    },
    .expect = {
      .entries = {
        { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
        { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "V", .rev = "R" } },
      },
      .moved = {
        {
          .was = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } },
          .now = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } },
        },
        {
          .was = { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "V", .rev = "R" } },
          .now = { .name = "B", .version = { 1, 0, 0 }, .source = { .url = "V", .rev = "S" } },
        },
      },
    },
  },
  {
    .name = "follower_replaces_the_entry",
    .kept = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" } } },
    .picks = { { .entry = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } }, .follows = true } },
    .expect.entries = { { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "S" } } },
  },
};

sp_test_each(lock, merge, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  u32 kept = 0;
  sp_carr_detect_len(it->kept, kept, it->kept[kept].name);
  spn_lock_t lock = lock_of(mem, it->kept, kept);

  si_da(si_lock_pick_t) picks = sp_zero;
  u32 picked = 0;
  sp_carr_detect_len(it->picks, picked, it->picks[picked].entry.name);
  sp_for(at, picked) {
    si_da_push(mem, picks, ((si_lock_pick_t) { .entry = entry(it->picks[at].entry), .follows = it->picks[at].follows }));
  }

  si_da(si_lock_move_t) moved = si_lock_merge(mem, &lock, picks);

  u32 entries = 0;
  sp_carr_detect_len(it->expect.entries, entries, it->expect.entries[entries].name);
  sp_try(expect_entries(t, &lock, it->expect.entries, entries));

  u32 moves = 0;
  sp_carr_detect_len(it->expect.moved, moves, it->expect.moved[moves].was.name);
  sp_must_eq(t, moves, (u32)si_da_size(moved));
  sp_for(at, moves) {
    expect_entry(t, moved[at].was, it->expect.moved[at].was);
    expect_entry(t, moved[at].now, it->expect.moved[at].now);
  }
  return SP_OK;
}
