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
  sp_err_t err = sp_fs_remove_dir_at(path);
  if (err == SP_ERR_SYS_NOT_DIR) {
    err = sp_fs_remove_file_at(path);
  }
  switch (err) {
    case SP_ERR_SYS_NOT_FOUND:
    case SP_ERR_SYS_NOT_DIR:   return SP_OK;
    default:                   return err;
  }
}

sp_str_t si_env_get(sp_env_t* env, sp_str_t name) {
  u64 index = 0;
  sp_str_t* value = sp_ht_get_ex(env->vars, name, index);
  return value ? *value : sp_zero_s(sp_str_t);
}
