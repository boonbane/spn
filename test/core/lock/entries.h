#ifndef SPN_TEST_LOCK_ENTRIES_H
#define SPN_TEST_LOCK_ENTRIES_H

#include "spn_test.h"

#include "intern/intern.h"
#include "lock/lock.h"
#include "semver/compare.h"

#define LOCK_TEST_MAX_ENTRIES 4
#define LOCK_TEST_MAX_INDEXES 2

typedef struct {
  const c8* name;
  const c8* url;
  const c8* rev;
} pin_t;

typedef struct {
  const c8* url;
  const c8* rev;
  const c8* dir;
} root_t;

typedef struct {
  const c8* name;
  spn_semver_t version;
  root_t source;
  root_t manifest;
} entry_t;

static inline sp_str_t text(const c8* str) {
  return str ? sp_cstr_as_str(str) : sp_str_lit("");
}

static inline spn_lock_root_t root(root_t literal) {
  return (spn_lock_root_t) { .url = text(literal.url), .rev = text(literal.rev), .dir = text(literal.dir) };
}

static inline spn_lock_entry_t entry(entry_t literal) {
  return (spn_lock_entry_t) {
    .name = spn_intern(sp_cstr_as_str(literal.name)),
    .version = literal.version,
    .source = root(literal.source),
    .manifest = root(literal.manifest),
  };
}

static inline spn_lock_t lock_of(sp_mem_t mem, const entry_t* entries, u32 len) {
  spn_lock_t lock = spn_lock_new(mem);
  sp_for(it, len) {
    spn_lock_insert(&lock, entry(entries[it]));
  }
  return lock;
}

static inline void pin_all(spn_lock_t* lock, const pin_t* pins, u32 len) {
  sp_for(it, len) {
    spn_lock_pin_index(lock, (spn_lock_index_t) {
      .name = spn_intern(sp_cstr_as_str(pins[it].name)),
      .url = text(pins[it].url),
      .rev = text(pins[it].rev),
    });
  }
}

static inline sp_err_t expect_pins(sp_test_t* t, const spn_lock_t* lock, const pin_t* expected, u32 len) {
  sp_must_eq(t, len, sp_da_size(lock->indexes));
  sp_for(it, len) {
    sp_expect_str_eq_c(t, lock->indexes[it].name.str, expected[it].name);
    sp_expect_str_eq(t, lock->indexes[it].url, text(expected[it].url));
    sp_expect_str_eq(t, lock->indexes[it].rev, text(expected[it].rev));
  }
  return SP_OK;
}

static inline void expect_root(sp_test_t* t, spn_lock_root_t actual, root_t expected) {
  sp_expect_str_eq(t, actual.url, text(expected.url));
  sp_expect_str_eq(t, actual.rev, text(expected.rev));
  sp_expect_str_eq(t, actual.dir, text(expected.dir));
}

static inline sp_err_t expect_entries(sp_test_t* t, const spn_lock_t* lock, const entry_t* expected, u32 len) {
  sp_must_eq(t, len, sp_da_size(lock->entries));
  sp_for(it, len) {
    sp_expect_str_eq_c(t, lock->entries[it].name.str, expected[it].name);
    sp_expect(t, spn_semver_eq(lock->entries[it].version, expected[it].version));
    expect_root(t, lock->entries[it].source, expected[it].source);
    expect_root(t, lock->entries[it].manifest, expected[it].manifest);
  }
  return SP_OK;
}

#endif
