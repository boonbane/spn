#ifndef SPN_LOCK_TYPES_H
#define SPN_LOCK_TYPES_H

#include "sp.h"
#include "spn/core.h"

#include "intern/types.h"
#include "semver/types.h"

#define spn_lock_version 1

typedef struct {
  sp_str_t url;
  sp_str_t rev;
  sp_str_t dir;
} spn_lock_root_t;

typedef struct {
  sp_intern_str_t name;
  spn_semver_t version;
  spn_lock_root_t source;
  spn_lock_root_t manifest;
} spn_lock_entry_t;

typedef struct {
  u64 version;
  sp_da(spn_lock_entry_t) entries;
} spn_lock_t;

typedef struct {
  const spn_lock_t* held;
  const spn_lock_t* next;
  struct {
    u32 held;
    u32 next;
  } at;
  const spn_lock_entry_t* was;
  const spn_lock_entry_t* now;
} spn_lock_diff_t;

#endif
