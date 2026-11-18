#include "entries.h"

typedef struct {
  const c8* name;
  entry_t was;
  entry_t now;
  struct {
    root_t locked;
    root_t found;
  } expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "source",
    .was = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" }, .manifest = { .url = "V", .rev = "S" } },
    .now = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "T" }, .manifest = { .url = "V", .rev = "S" } },
    .expect = {
      .locked = { .url = "U", .rev = "R" },
      .found = { .url = "U", .rev = "T" },
    },
  },
  {
    .name = "manifest",
    .was = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" }, .manifest = { .url = "V", .rev = "S", .dir = "D" } },
    .now = { .name = "A", .version = { 1, 0, 0 }, .source = { .url = "U", .rev = "R" }, .manifest = { .url = "V", .rev = "S", .dir = "E" } },
    .expect = {
      .locked = { .url = "V", .rev = "S", .dir = "D" },
      .found = { .url = "V", .rev = "S", .dir = "E" },
    },
  },
};

sp_test_each(lock, mismatch, test_t, tests, .setup = spn_test_ctx_setup) {
  spn_lock_entry_t was = entry(it->was);
  spn_lock_entry_t now = entry(it->now);

  spn_err_union_t err = spn_lock_mismatch(&was, &now);
  sp_expect_eq(t, SPN_ERR_LOCK_MISMATCH, err.kind);
  sp_expect_str_eq_c(t, err.subject, it->was.name);
  sp_expect_str_eq(t, err.lock_mismatch.locked.url, text(it->expect.locked.url));
  sp_expect_str_eq(t, err.lock_mismatch.locked.rev, text(it->expect.locked.rev));
  sp_expect_str_eq(t, err.lock_mismatch.locked.dir, text(it->expect.locked.dir));
  sp_expect_str_eq(t, err.lock_mismatch.found.url, text(it->expect.found.url));
  sp_expect_str_eq(t, err.lock_mismatch.found.rev, text(it->expect.found.rev));
  sp_expect_str_eq(t, err.lock_mismatch.found.dir, text(it->expect.found.dir));
  return SP_OK;
}
