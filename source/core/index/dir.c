#include "index/dir.h"

#include "error/error.h"
#include "index/release.h"
#include "paths/paths.h"
#include "pkg/load.h"
#include "semver/compare.h"

static bool has_manifest(const spn_path_roots_t* roots, spn_path_t path) {
  sp_sys_file_meta_t meta = sp_zero;
  return !sp_fs_get_path_metadata_at(spn_path_at(roots, path), &meta) && meta.kind != SP_FS_KIND_UNKNOWN;
}

static bool holds_manifest(sp_mem_t mem, sp_path_t dir, u32 depth) {
  sp_sys_dir_t handle = sp_zero;
  if (sp_fs_open_dir_at(dir, &handle)) {
    return false;
  }

  SP_ALIGNED u8 buffer [SP_SYS_DIR_MIN_BUF] = sp_zero;
  sp_fs_dir_it_t it = sp_fs_dir_it(handle);
  sp_fs_dir_entry_t entry = sp_zero;
  bool found = false;
  while (!found && sp_fs_dir_it_next(&it, sp_mem_slice(buffer, sizeof(buffer)), &entry)) {
    if (sp_str_starts_with(entry.name, sp_str_lit("."))) {
      continue;
    }
    sp_path_t child = sp_path_join(mem, dir, entry.name);
    sp_sys_file_meta_t meta = sp_zero;
    found = !sp_fs_get_path_metadata_at(sp_path_join(mem, child, sp_str_lit("spn.toml")), &meta) && meta.kind != SP_FS_KIND_UNKNOWN;
    if (!found && depth) {
      found = holds_manifest(mem, child, depth - 1);
    }
  }
  sp_fs_dir_it_close(&it);
  sp_fs_close_dir(handle);
  return found;
}

bool spn_index_dir_has_packages(sp_mem_t mem, spn_index_info_t* index) {
  return holds_manifest(mem, index->location.at, 1);
}

static spn_err_t add_release(const si_index_place_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_intern_str_t name, sp_str_t sub, spn_index_pkg_t* package, spn_err_union_t* err) {
  spn_path_t dir = spn_path_join(mem, (spn_path_t) { .sub = index->dir }, sub);
  spn_path_t path = spn_path_join(mem, dir, sp_str_lit("spn.toml"));
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
  if (!sp_str_empty(index->git.url)) {
    published = (spn_pkg_root_t) {
      .kind = SPN_PKG_ROOT_GIT,
      .git = { .url = index->git.url, .rev = index->git.rev, .dir = sp_fs_join_path(mem, index->git.dir, sub) },
    };
  }

  spn_index_release_t release = sp_zero;
  spn_try(spn_index_release_from_manifest(mem, manifest, published, &release, err));
  release.follows = published.kind == SPN_PKG_ROOT_GIT;
  sp_da_push(package->releases, release);
  return SPN_OK;
}

spn_err_t si_index_dir_get_package(const si_index_place_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err) {
  *pkg = SP_NULLPTR;

  spn_path_t dir = spn_path_join(mem, (spn_path_t) { .sub = index->dir }, name.str);
  spn_path_t flat = spn_path_join(mem, dir, sp_str_lit("spn.toml"));
  if (has_manifest(roots, flat)) {
    spn_index_pkg_t* package = sp_alloc_type(mem, spn_index_pkg_t);
    sp_da_init(mem, package->releases);
    spn_try(add_release(index, mem, intern, roots, name, name.str, package, err));
    *pkg = package;
    return SPN_OK;
  }

  sp_sys_dir_t handle = sp_zero;
  if (sp_fs_open_dir_at(spn_path_at(roots, dir), &handle)) {
    return SPN_OK;
  }

  spn_index_pkg_t* package = sp_alloc_type(mem, spn_index_pkg_t);
  sp_da_init(mem, package->releases);

  SP_ALIGNED u8 buffer [SP_SYS_DIR_MIN_BUF] = sp_zero;
  sp_fs_dir_it_t it = sp_fs_dir_it(handle);
  sp_fs_dir_entry_t entry = sp_zero;
  spn_err_t result = SPN_OK;
  while (!result && sp_fs_dir_it_next(&it, sp_mem_slice(buffer, sizeof(buffer)), &entry)) {
    spn_path_t versioned = spn_path_join(mem, spn_path_join(mem, dir, entry.name), sp_str_lit("spn.toml"));
    if (has_manifest(roots, versioned)) {
      result = add_release(index, mem, intern, roots, name, sp_fs_join_path(mem, name.str, entry.name), package, err);
    }
  }
  sp_fs_dir_it_close(&it);
  sp_fs_close_dir(handle);
  spn_try(result);

  if (it.err) {
    *err = (spn_err_union_t) { .kind = SPN_ERR_FS_READ, .fs = { .path = dir } };
    return err->kind;
  }
  if (sp_da_empty(package->releases)) {
    return SPN_OK;
  }

  sp_da_for(package->releases, at) {
    sp_for(prior, at) {
      if (spn_semver_eq(package->releases[prior].version, package->releases[at].version)) {
        *err = (spn_err_union_t) {
          .kind = SPN_ERR_INDEX_CORRUPT,
          .index_corrupt = { .name = name.str, .path = spn_path_str(roots, mem, dir) },
        };
        return err->kind;
      }
    }
  }

  *pkg = package;
  return SPN_OK;
}
