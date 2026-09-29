#define SP_PRIVATE_HEADER
#include "fs/fs.h"

#if defined(SP_MACOS) || defined(SP_COSMO)
  #include <sys/file.h>
  #include <errno.h>
#endif

#if defined(SP_LINUX) && !defined(SP_SYSCALL_NUM_FLOCK)
  #if defined(SP_AMD64)
    #define SP_SYSCALL_NUM_FLOCK 73
  #elif defined(SP_ARM64)
    #define SP_SYSCALL_NUM_FLOCK 32
  #endif
#endif

#if !defined(SP_EAGAIN)
  #define SP_EAGAIN EAGAIN
#endif

s32 sp_sys_flock(sp_sys_fd_t fd, s32 op) {
#if defined(SP_WIN32)
  OVERLAPPED overlapped = sp_zero;
  if (op & SP_LOCK_UN) {
    return UnlockFileEx((HANDLE)fd, 0, MAXDWORD, MAXDWORD, &overlapped) ? 0 : -1;
  }

  u32 flags = 0;
  if (op & SP_LOCK_EX) flags |= LOCKFILE_EXCLUSIVE_LOCK;
  if (op & SP_LOCK_NB) flags |= LOCKFILE_FAIL_IMMEDIATELY;
  if (LockFileEx((HANDLE)fd, flags, 0, MAXDWORD, MAXDWORD, &overlapped)) {
    return 0;
  }

  u32 error = GetLastError();
  return (error == ERROR_LOCK_VIOLATION || error == ERROR_IO_PENDING) ? -(s32)SP_EAGAIN : -1;

#elif defined(SP_LINUX)
  s32 rc;
  do {
    rc = (s32)sp_syscall(SP_SYSCALL_NUM_FLOCK, fd, op);
  } while (rc == -SP_EINTR);
  return rc;

#elif defined(SP_MACOS) || defined(SP_COSMO)
  s32 rc;
  do {
    rc = flock((s32)fd, op);
  } while (rc == -1 && errno == SP_EINTR);
  return rc == -1 ? -errno : rc;

#else
  #error "sp_sys_flock"
#endif
}

static sp_err_t sp_fs_lock_open(sp_fs_lock_t* lock, sp_path_t path) {
  *lock = sp_zero_s(sp_fs_lock_t);
  if (sp_sys_open_s(path.dir, path.sub, SP_SYS_OPEN_MODE_RW, SP_SYS_OPEN_CREATE, &lock->fd)) {
    return SP_ERR_SYS;
  }
  return SP_OK;
}

static void sp_fs_lock_drop(sp_fs_lock_t* lock) {
  sp_sys_close(lock->fd);
  *lock = sp_zero_s(sp_fs_lock_t);
}

sp_err_t sp_fs_lock_acquire(sp_fs_lock_t* lock, sp_path_t path) {
  sp_try(sp_fs_lock_open(lock, path));

  if (sp_sys_flock(lock->fd, SP_LOCK_EX)) {
    sp_fs_lock_drop(lock);
    return SP_ERR_SYS;
  }

  lock->held = true;
  return SP_OK;
}

sp_err_t sp_fs_lock_try_acquire(sp_fs_lock_t* lock, sp_path_t path, bool* acquired) {
  *acquired = false;
  sp_try(sp_fs_lock_open(lock, path));

  s32 rc = sp_sys_flock(lock->fd, SP_LOCK_EX | SP_LOCK_NB);
  if (rc) {
    sp_err_t err = rc == -(s32)SP_EAGAIN ? SP_OK : SP_ERR_SYS;
    sp_fs_lock_drop(lock);
    return err;
  }

  lock->held = true;
  *acquired = true;
  return SP_OK;
}

sp_err_t sp_fs_lock_release(sp_fs_lock_t* lock) {
  if (!lock->held) return SP_OK;

  sp_sys_flock(lock->fd, SP_LOCK_UN);
  sp_fs_lock_drop(lock);
  return SP_OK;
}

sp_err_t sp_fs_create_parent(sp_path_t path) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_err_t err = sp_fs_create_dir_at(sp_path_parent(s.mem, path));
  sp_mem_end_scratch(s);
  return err;
}

sp_err_t sp_fs_append(sp_path_t path, sp_str_t str) {
  sp_sys_fd_t fd = SP_SYS_INVALID_FD;
  sp_try(sp_sys_open_s(path.dir, path.sub, SP_SYS_OPEN_MODE_WO, SP_SYS_OPEN_CREATE | SP_SYS_OPEN_APPEND, &fd));
  sp_io_stream_writer_t io = sp_zero;
  sp_io_stream_writer_from_fd(&io, fd, SP_IO_CLOSE_MODE_AUTO);
  sp_err_t written = sp_io_write_all(&io.base, str.data, str.len, SP_NULLPTR);
  sp_err_t closed = sp_io_stream_writer_close(&io);
  return written ? written : closed;
}

sp_err_t sp_fs_set_readonly(sp_path_t path) {
  sp_sys_file_meta_t meta = sp_zero;
  sp_try(sp_sys_get_path_metadata_s(path.dir, path.sub, &meta));
  sp_sys_set_read_only(&meta.perms, true);
  return sp_sys_set_file_perms_s(path.dir, path.sub, meta.perms);
}

sp_err_t sp_fs_set_writable(sp_path_t path) {
  sp_sys_file_meta_t meta = sp_zero;
  sp_try(sp_sys_get_path_metadata_s(path.dir, path.sub, &meta));
  sp_sys_set_read_only(&meta.perms, false);
  return sp_sys_set_file_perms_s(path.dir, path.sub, meta.perms);
}
