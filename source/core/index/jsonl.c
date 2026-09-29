#include "index/jsonl.h"

#include "ctx/types.h"
#include "index/json.h"
#include "paths/paths.h"
#include "pkg/id.h"
#include "io/io.h"

spn_path_t spn_index_jsonl_path(sp_mem_t mem, spn_index_info_t* index, spn_pkg_name_t id) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  sp_str_t file = sp_fmt(scratch.mem, "{}.jsonl", sp_fmt_str(id.name)).value;
  sp_str_t relative = sp_fs_join_path(scratch.mem, id.namespace, file);
  spn_path_t path = spn_path_join(mem, index->location, relative);
  sp_mem_end_scratch(scratch);
  return path;
}

spn_err_t spn_index_jsonl_get_package(spn_index_info_t* index, sp_mem_t mem, spn_pkg_name_t id, spn_index_pkg_t** pkg, spn_index_diag_t* diag) {
  *pkg = SP_NULLPTR;

  spn_err_t result = SPN_OK;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);

  spn_path_t path = spn_index_jsonl_path(scratch.mem, index, id);
  sp_path_t at = spn_path_at(&spn.roots, path);
  if (!sp_fs_exists_at(at)) {
    goto cleanup;
  }

  sp_str_t blob = sp_zero;
  sp_io_read_file_at(scratch.mem, at, &blob);

  spn_index_pkg_t* package = sp_alloc_type(mem, spn_index_pkg_t);
  *package = SP_ZERO_STRUCT(spn_index_pkg_t);
  if (spn_index_parse_pkg(mem, id, blob, package) != SPN_OK) {
    diag->path = spn_path_str(&spn.roots, mem, path);
    result = SPN_ERR_INDEX_CORRUPT;
    goto cleanup;
  }

  *pkg = package;

cleanup:
  sp_mem_end_scratch(scratch);
  return result;
}
