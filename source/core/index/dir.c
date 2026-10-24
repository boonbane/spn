#include "index/dir.h"

#include "error/error.h"
#include "index/release.h"
#include "paths/paths.h"
#include "pkg/id.h"
#include "pkg/load.h"

spn_err_t spn_index_dir_get_package(spn_index_info_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_pkg_name_t id, spn_index_pkg_t** pkg, spn_err_union_t* err) {
  *pkg = SP_NULLPTR;

  spn_path_t dir = spn_path_join(mem, index->dir.path, id.name);
  spn_path_t path = spn_path_join(mem, dir, sp_str_lit("spn.toml"));

  si_pkg_info_t* manifest = sp_alloc_type(mem, si_pkg_info_t);
  spn_err_t loaded = spn_pkg_load(mem, intern, roots, path, manifest, err);
  if (loaded == SPN_ERR_NO_MANIFEST) {
    return SPN_OK;
  }
  if (loaded) {
    *err = spn_err_about(*err, spn_pkg_name_to_qualified(id));
    return loaded;
  }

  if (!sp_str_equal(manifest->name.str, id.name)) {
    *err = (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_CORRUPT,
      .index_corrupt = { .name = spn_pkg_name_to_qualified(id), .path = spn_path_str(roots, mem, path) },
    };
    return err->kind;
  }

  if (!sp_str_equal(manifest->qualified.str, spn_pkg_name_to_qualified(id))) {
    return SPN_OK;
  }

  spn_pkg_root_t published = { .kind = SPN_PKG_ROOT_LOCAL, .local = dir };

  spn_index_release_t release = sp_zero;
  spn_try(spn_index_release_from_manifest(mem, manifest, published, &release, err));

  spn_index_pkg_t* package = sp_alloc_type(mem, spn_index_pkg_t);
  *package = (spn_index_pkg_t) { .id = id };
  sp_da_init(mem, package->releases);
  sp_da_push(package->releases, release);
  *pkg = package;
  return SPN_OK;
}
