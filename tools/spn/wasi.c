#include "sp.h"

static sp_path_t resolve(sp_sys_dir_t dir, const c8* path, u32 len) {
  sp_str_t str = sp_str(path, len);
  return sp_fs_is_absolute(str) ? sp_path_from_str(str) : sp_path(dir, str);
}

static bool rooted(sp_path_t path) {
  return path.dir.fd != SP_SYS_INVALID_FD;
}

static sp_err_t wasi_open(sp_sys_dir_t dir, const c8* path, u32 len, sp_sys_open_mode_t mode, u32 flags, sp_sys_fd_t* out) {
  *out = SP_SYS_INVALID_FD;
  sp_path_t at = resolve(dir, path, len);
  if (!rooted(at)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_open_p(at.dir, at.sub.data, at.sub.len, mode, flags, out);
}

static sp_err_t wasi_open_dir(sp_sys_dir_t dir, const c8* path, u32 len, u32 flags, sp_sys_dir_t* out) {
  out->fd = SP_SYS_INVALID_FD;
  sp_path_t at = resolve(dir, path, len);
  if (!rooted(at)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_open_dir_p(at.dir, at.sub.data, at.sub.len, flags, out);
}

static sp_err_t wasi_mkdir(sp_sys_dir_t dir, const c8* path, u32 len, sp_sys_file_perms_t perms) {
  sp_path_t at = resolve(dir, path, len);
  if (!rooted(at)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_mkdir_p(at.dir, at.sub.data, at.sub.len, perms);
}

static sp_err_t wasi_rmdir(sp_sys_dir_t dir, const c8* path, u32 len) {
  sp_path_t at = resolve(dir, path, len);
  if (!rooted(at)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_rmdir_p(at.dir, at.sub.data, at.sub.len);
}

static sp_err_t wasi_unlink(sp_sys_dir_t dir, const c8* path, u32 len) {
  sp_path_t at = resolve(dir, path, len);
  if (!rooted(at)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_unlink_p(at.dir, at.sub.data, at.sub.len);
}

static sp_err_t wasi_rename(sp_sys_dir_t from_dir, const c8* from, u32 from_len, sp_sys_dir_t to_dir, const c8* to, u32 to_len) {
  sp_path_t src = resolve(from_dir, from, from_len);
  sp_path_t dst = resolve(to_dir, to, to_len);
  if (!rooted(src) || !rooted(dst)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_rename_p(src.dir, src.sub.data, src.sub.len, dst.dir, dst.sub.data, dst.sub.len);
}

static sp_err_t wasi_link(sp_sys_dir_t from_dir, const c8* existing, u32 existing_len, sp_sys_dir_t to_dir, const c8* alias, u32 alias_len) {
  sp_path_t src = resolve(from_dir, existing, existing_len);
  sp_path_t dst = resolve(to_dir, alias, alias_len);
  if (!rooted(src) || !rooted(dst)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_link_p(src.dir, src.sub.data, src.sub.len, dst.dir, dst.sub.data, dst.sub.len);
}

static sp_err_t wasi_symlink(const c8* existing, u32 existing_len, sp_sys_dir_t to_dir, const c8* alias, u32 alias_len, sp_fs_kind_t kind) {
  sp_path_t dst = resolve(to_dir, alias, alias_len);
  if (!rooted(dst)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_symlink_p(existing, existing_len, dst.dir, dst.sub.data, dst.sub.len, kind);
}

static sp_err_t wasi_get_path_metadata(sp_sys_dir_t dir, const c8* path, u32 len, sp_sys_file_meta_t* st) {
  sp_path_t at = resolve(dir, path, len);
  if (!rooted(at)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_get_path_metadata_p(at.dir, at.sub.data, at.sub.len, st);
}

static sp_err_t wasi_get_link_metadata(sp_sys_dir_t dir, const c8* path, u32 len, sp_sys_file_meta_t* st) {
  sp_path_t at = resolve(dir, path, len);
  if (!rooted(at)) return SP_ERR_SYS_NOT_FOUND;
  return sp_sys_get_link_metadata_p(at.dir, at.sub.data, at.sub.len, st);
}

static sp_sys_vtable_t vtable;

__attribute__((constructor))
static void install() {
  vtable = *sp_rt.vt;
  vtable.open = wasi_open;
  vtable.open_dir = wasi_open_dir;
  vtable.mkdir = wasi_mkdir;
  vtable.rmdir = wasi_rmdir;
  vtable.unlink = wasi_unlink;
  vtable.rename = wasi_rename;
  vtable.link = wasi_link;
  vtable.symlink = wasi_symlink;
  vtable.get_path_metadata = wasi_get_path_metadata;
  vtable.get_link_metadata = wasi_get_link_metadata;
  sp_sys_set_vtable(&vtable);
}
