#ifndef SPN_INDEX_JSONL_H
#define SPN_INDEX_JSONL_H

#include "sp.h"
#include "spn/core.h"
#include "index/types.h"
#include "spn/errors.h"

sp_str_t  spn_index_jsonl_path(sp_mem_t mem, sp_intern_str_t name);
spn_err_t spn_index_jsonl_get_package(spn_index_info_t* index, sp_mem_t mem, sp_intern_t* intern, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err);

#endif
