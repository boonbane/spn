#ifndef SPN_INDEX_MARKER_H
#define SPN_INDEX_MARKER_H

#include "sp.h"
#include "spn/core.h"
#include "spn/errors.h"
#include "spn/types.h"
#include "intern/types.h"
#include "paths/types.h"

#define si_index_marker_file "spn.index.toml"
#define si_index_marker_version 1

typedef struct {
  u64 version;
  spn_index_format_t format;
  sp_str_t path;
} si_index_marker_t;

spn_err_t si_index_marker_load(sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_path_t dir, si_index_marker_t* marker, spn_err_union_t* err);
spn_err_t si_index_marker_save(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t dir, si_index_marker_t marker, spn_err_union_t* err);

#endif
