#include "entries.h"

typedef struct {
  const c8* name;
  pin_t held [LOCK_TEST_MAX_INDEXES];
  pin_t next [LOCK_TEST_MAX_INDEXES];
  struct {
    const c8* drift;
  } expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "same",
    .held = { { .name = "I", .url = "U", .rev = "R" } },
    .next = { { .name = "I", .url = "U", .rev = "R" } },
  },
  {
    .name = "added",
    .next = { { .name = "I", .url = "U", .rev = "R" } },
    .expect.drift = "I",
  },
  {
    .name = "removed",
    .held = { { .name = "I", .url = "U", .rev = "R" } },
    .expect.drift = "I",
  },
  {
    .name = "moved",
    .held = { { .name = "I", .url = "U", .rev = "R" } },
    .next = { { .name = "I", .url = "U", .rev = "S" } },
    .expect.drift = "I",
  },
  {
    .name = "rehosted",
    .held = { { .name = "I", .url = "U", .rev = "R" } },
    .next = { { .name = "I", .url = "V", .rev = "R" } },
    .expect.drift = "I",
  },
};

sp_test_each(lock, index_drift, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  spn_lock_t held = spn_lock_new(mem);
  pin_all(&held, it->held);
  spn_lock_t next = spn_lock_new(mem);
  pin_all(&next, it->next);

  const spn_lock_index_t* drift = spn_lock_index_drift(&held, &next);
  sp_must_eq(t, it->expect.drift != SP_NULLPTR, drift != SP_NULLPTR);
  if (drift) {
    sp_expect_str_eq_c(t, drift->name.str, it->expect.drift);
  }
  return SP_OK;
}
