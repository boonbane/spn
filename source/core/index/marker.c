#include "index/marker.h"

#include "enum/enum.h"
#include "index_marker.gen.h"
#include "paths/paths.h"
#include "toml/edit.h"
#include "toml/loader.h"

spn_err_t si_index_marker_load(sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_path_t dir, si_index_marker_t* marker, spn_err_union_t* err) {
  *marker = sp_zero_s(si_index_marker_t);

  spn_path_t path = spn_path_join(mem, dir, sp_str_lit(si_index_marker_file));
  sp_sys_file_meta_t meta = sp_zero;
  if (sp_fs_get_path_metadata_at(spn_path_at(roots, path), &meta) || meta.kind == SP_FS_KIND_UNKNOWN) {
    return SPN_OK;
  }

  spn_toml_loader_t t = spn_toml_loader(mem, intern, roots);
  spn_cg_index_marker_t cg = sp_zero;
  spn_toml_load_index_marker(&t, path, &cg);
  if (cg.version > si_index_marker_version) {
    *err = (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_VERSION,
      .index_version = { .path = spn_path_str(roots, mem, path), .version = cg.version, .supported = si_index_marker_version },
    };
    return err->kind;
  }

  bool manifests = sp_str_equal(cg.format, spn_index_format_to_str(SPN_INDEX_FORMAT_MANIFESTS));
  bool releases = sp_str_equal(cg.format, spn_index_format_to_str(SPN_INDEX_FORMAT_RELEASES));
  if (sp_da_empty(t.issues) && !manifests && !releases) {
    *err = (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_FORMAT,
      .index_format = { .path = spn_path_str(roots, mem, path), .format = cg.format },
    };
    return err->kind;
  }

  c8* sub = sp_alloc_n(mem, c8, cg.path.len);
  u32 len = 0;
  bool inside = !sp_fs_is_absolute(cg.path) && sp_str_find_c8(cg.path, '\\') < 0;
  sp_str_pair_t parts = { .second = cg.path };
  while (!sp_str_empty(parts.second)) {
    parts = sp_str_cleave_c8(parts.second, '/');
    if (sp_str_empty(parts.first) || sp_str_equal_cstr(parts.first, ".")) {
      continue;
    }
    inside = inside && !sp_str_equal_cstr(parts.first, "..");
    if (len) {
      sub[len++] = '/';
    }
    sp_mem_copy(sub + len, parts.first.data, parts.first.len);
    len += parts.first.len;
  }
  if (!inside) {
    spn_toml_loader_issue(&t, SPN_ERR_CODEGEN_INVALID, "path");
  }
  if (sp_da_empty(t.issues) && !cg.version) {
    spn_toml_loader_issue(&t, SPN_ERR_CODEGEN_INVALID, "version");
  }
  if (!sp_da_empty(t.issues)) {
    *err = spn_toml_loader_err(&t, path);
    err->kind = SPN_ERR_INDEX_MARKER_INVALID;
    return err->kind;
  }

  *marker = (si_index_marker_t) {
    .version = cg.version,
    .format = manifests ? SPN_INDEX_FORMAT_MANIFESTS : SPN_INDEX_FORMAT_RELEASES,
    .path = sp_str(sub, len),
  };
  return SPN_OK;
}

spn_err_t si_index_marker_save(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t dir, si_index_marker_t marker, spn_err_union_t* err) {
  spn_path_t path = spn_path_join(mem, dir, sp_str_lit(si_index_marker_file));
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_io_dyn_mem_writer_t out = sp_zero;
  sp_io_dyn_mem_writer_init(scratch.mem, &out);

  sp_fmt_io(&out.base, "version = {}\nformat = ", sp_fmt_uint(marker.version));
  spn_toml_write_str(&out.base, spn_index_format_to_str(marker.format));
  sp_io_write_c8(&out.base, '\n');
  if (!sp_str_empty(marker.path)) {
    sp_io_write_cstr(&out.base, "path = ", SP_NULLPTR);
    spn_toml_write_str(&out.base, marker.path);
    sp_io_write_c8(&out.base, '\n');
  }

  sp_err_t written = sp_fs_write_at(spn_path_at(roots, path), sp_io_dyn_mem_writer_as_str(&out), SP_FS_WRITE_REPLACE, sp_sys_default_file_perms);
  sp_mem_end_scratch(scratch);
  if (written) {
    *err = (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = path } };
    return err->kind;
  }
  return SPN_OK;
}
