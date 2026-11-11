#include "index/dir.h"

#include "error/error.h"
#include "index/release.h"
#include "paths/paths.h"
#include "pkg/load.h"

spn_err_t spn_index_dir_get_package(spn_index_info_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err) {
  *pkg = SP_NULLPTR;

  spn_path_t dir = spn_path_join(mem, (spn_path_t) { .sub = index->location.dir }, name.str);
  spn_path_t path = spn_path_join(mem, dir, sp_str_lit("spn.toml"));
  if (!sp_fs_exists_at(spn_path_at(roots, path))) {
    return SPN_OK;
  }

  si_pkg_info_t* manifest = sp_alloc_type(mem, si_pkg_info_t);
  if (spn_pkg_load(mem, intern, roots, path, manifest, err)) {
    *err = spn_err_about(*err, name.str);
    return err->kind;
  }

  if (manifest->name.id != name.id) {
    *err = (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_CORRUPT,
      .index_corrupt = { .name = name.str, .path = spn_path_str(roots, mem, path) },
    };
    return err->kind;
  }

  spn_pkg_root_t published = { .kind = SPN_PKG_ROOT_LOCAL, .local = dir };

  spn_index_release_t release = sp_zero;
  spn_try(spn_index_release_from_manifest(mem, manifest, published, &release, err));

  spn_index_pkg_t* package = sp_alloc_type(mem, spn_index_pkg_t);
  sp_da_init(mem, package->releases);
  sp_da_push(package->releases, release);
  *pkg = package;
  return SPN_OK;
}
