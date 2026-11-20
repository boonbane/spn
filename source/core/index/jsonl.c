#include "index/jsonl.h"

#include "index/json.h"
#include "io/io.h"

sp_str_t si_index_jsonl_path(sp_mem_t mem, sp_intern_str_t name) {
  return sp_fmt(mem, "{}.jsonl", sp_fmt_str(name.str)).value;
}

spn_err_t si_index_jsonl_get_package(const si_index_place_t* index, sp_mem_t mem, sp_intern_t* intern, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err) {
  *pkg = SP_NULLPTR;

  spn_err_t result = SPN_OK;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);

  sp_str_t path = si_index_jsonl_path(scratch.mem, name);
  sp_path_t at = sp_path_join(scratch.mem, index->at, path);
  sp_sys_file_meta_t meta = sp_zero;
  if (sp_fs_get_path_metadata_at(at, &meta) || meta.kind == SP_FS_KIND_UNKNOWN) {
    goto cleanup;
  }

  sp_str_t blob = sp_zero;
  sp_fs_read_at(scratch.mem, at, &blob);

  spn_index_pkg_t* package = sp_alloc_type(mem, spn_index_pkg_t);
  if (spn_index_parse_pkg(mem, intern, name, blob, package) != SPN_OK) {
    *err = (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_CORRUPT,
      .index_corrupt = { .name = name.str, .path = sp_fs_join_path(mem, index->dir, path) },
    };
    result = err->kind;
    goto cleanup;
  }

  *pkg = package;

cleanup:
  sp_mem_end_scratch(scratch);
  return result;
}
