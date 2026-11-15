#include "os/os.h"
#include "str/str.h"

#if defined(SP_WIN32)
sp_str_t sp_fs_get_home_path(sp_mem_t mem) {
  sp_str_t drive = sp_os_env_get(sp_str_lit("HOMEDRIVE"));
  SP_ASSERT(!sp_str_empty(drive));
  sp_str_t path = sp_os_env_get(sp_str_lit("HOMEPATH"));
  SP_ASSERT(!sp_str_empty(path));
  return sp_str_concat(mem, drive, sp_fs_normalize_path(mem, path));
}
#else
sp_str_t sp_fs_get_home_path(sp_mem_t mem) {
  sp_str_t path = sp_os_env_get(sp_str_lit("HOME"));
  SP_ASSERT(!sp_str_empty(path));
  return sp_fs_normalize_path(mem, path);
}
#endif

sp_err_t sp_fs_remove(sp_path_t path) {
  sp_sys_file_meta_t meta = sp_zero;
  if (sp_fs_get_link_metadata_at(path, &meta)) {
    return SP_OK;
  }
  switch (meta.kind) {
    case SP_FS_KIND_DIR:     return sp_fs_remove_dir_at(path);
    case SP_FS_KIND_FILE:
    case SP_FS_KIND_SYMLINK:
    case SP_FS_KIND_UNKNOWN: return sp_fs_remove_file_at(path);
    case SP_FS_KIND_NONE:    return SP_OK;
  }
  sp_unreachable_return(SP_OK);
}
