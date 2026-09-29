#ifndef SPN_PATHS_TYPES_H
#define SPN_PATHS_TYPES_H

#include "sp.h"
#include "spn/core.h"
#include "spn/types.h"
#include "intern/types.h"

typedef u32 spn_path_root_set_t;

typedef struct {
  spn_path_root_t root;
  sp_intern_id_t sub;
} spn_path_id_t;

_Static_assert(
  sizeof(spn_path_id_t) == sizeof(spn_path_root_t) + sizeof(sp_intern_id_t),
  "spn_path_id_t is byte-hashed as a key; it must have no padding"
);

typedef struct {
  sp_str_t dirs [SPN_PATH_ROOT_COUNT];
  sp_sys_fd_t fds [SPN_PATH_ROOT_COUNT];
  spn_path_root_set_t pinned;
} spn_path_roots_t;

typedef struct {
  bool within;
  sp_str_t sub;
} spn_path_rel_t;

typedef struct {
  spn_path_t recipe;
  spn_path_t source;
} spn_tree_roots_t;

typedef struct {
  spn_tree_t tree;
  sp_str_t sub;
} spn_tree_rel_t;

typedef struct {
  sp_str_t prefix;
  spn_path_t path;
} spn_arg_t;

typedef struct {
  sp_str_t patches;
  struct {
    sp_str_t dir;
    sp_str_t toml;
  } config;
} spn_system_paths_t;

#endif
