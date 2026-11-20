#ifndef SPN_LOCK_TYPES_H
#define SPN_LOCK_TYPES_H

#include "sp.h"
#include "spn/core.h"

#include "array/array.h"
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
  sp_intern_str_t name;
  sp_str_t url;
  sp_str_t rev;
} spn_lock_index_t;

typedef struct {
  u64 version;
  sp_da(spn_lock_index_t) indexes;
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

typedef struct {
  spn_lock_entry_t entry;
  bool pinned;
} si_lock_pick_t;

typedef struct {
  spn_lock_entry_t was;
  spn_lock_entry_t now;
} si_lock_move_t;

typedef struct {
  const spn_lock_t* held;
  const spn_lock_t* kept;
  si_da(si_lock_pick_t) picks;
} si_lock_request_t;

typedef struct {
  spn_lock_t lock;
  si_da(si_lock_move_t) moved;
  bool changed;
  sp_str_t stale;
} si_lock_next_t;

#endif
