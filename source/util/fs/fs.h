#ifndef SPN_SP_FS_H
#define SPN_SP_FS_H

#include "sp.h"

#define SP_LOCK_SH 1
#define SP_LOCK_EX 2
#define SP_LOCK_NB 4
#define SP_LOCK_UN 8

s32 sp_sys_flock(sp_sys_fd_t fd, s32 op);

typedef struct {
  sp_sys_fd_t fd;
  bool held;
} sp_fs_lock_t;

sp_err_t sp_fs_lock_acquire(sp_fs_lock_t* lock, sp_path_t path);
sp_err_t sp_fs_lock_try_acquire(sp_fs_lock_t* lock, sp_path_t path, bool* acquired);
sp_err_t sp_fs_lock_release(sp_fs_lock_t* lock);

sp_err_t sp_fs_append(sp_path_t path, sp_str_t str);
sp_err_t sp_fs_set_readonly(sp_path_t path);
sp_err_t sp_fs_set_writable(sp_path_t path);

#endif
