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

#define sp_fs_copy_buffer_size (64 * 1024)

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
  if (sp_sys_open_s(path.dir, path.sub, SP_SYS_OPEN_MODE_RW, SP_SYS_OPEN_CREATE, sp_sys_default_file_perms, &lock->fd)) {
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

sp_err_t sp_fs_replace_dir_at(sp_path_t fresh, sp_path_t live, sp_path_t retired) {
  sp_sys_file_meta_t meta = sp_zero;
  bool present = !sp_fs_get_path_metadata_at(live, &meta);
  if (present) {
    sp_try(sp_sys_rename_s(live.dir, live.sub, retired.dir, retired.sub, 0));
  }

  sp_err_t placed = sp_sys_rename_s(fresh.dir, fresh.sub, live.dir, live.sub, 0);
  if (placed && present) {
    sp_sys_rename_s(retired.dir, retired.sub, live.dir, live.sub, 0);
  }
  if (!placed) {
    sp_fs_remove_dir_at(retired);
  }
  return placed;
}

sp_err_t sp_fs_set_readonly(sp_path_t path) {
  sp_sys_file_meta_t meta = sp_zero;
  sp_try(sp_sys_get_path_metadata_s(path.dir, path.sub, &meta));
  sp_sys_set_read_only(&meta.perms, true);
  return sp_sys_set_file_perms_s(path.dir, path.sub, meta.perms);
}

sp_err_t sp_fs_copy_file_buffered_at(sp_path_t from, sp_path_t to) {
  sp_io_file_reader_t reader = sp_zero;
  sp_try(sp_io_file_reader_from_path_at(&reader, from));

  sp_sys_file_meta_t meta = sp_zero;
  sp_err_t err = sp_sys_get_file_metadata(reader.file, &meta);
  if (!err && meta.kind != SP_FS_KIND_FILE) {
    err = meta.kind == SP_FS_KIND_DIR ? SP_ERR_SYS_IS_DIR : SP_ERR_SYS_UNSUPPORTED;
  }
  sp_sys_set_read_only(&meta.perms, false);

  sp_fs_atomic_t af = sp_zero;
  if (!err) {
    err = sp_fs_atomic_open_at(&af, to, meta.perms);
  }
  if (err) {
    sp_io_file_reader_close(&reader);
    return err;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_io_writer_t* writer = sp_fs_atomic_writer(&af);
  sp_io_reader_set_buffer(&reader.base, sp_alloc_n(s.mem, u8, sp_fs_copy_buffer_size), sp_fs_copy_buffer_size);
  err = sp_io_writer_set_buffer(writer, sp_alloc_n(s.mem, u8, sp_fs_copy_buffer_size), sp_fs_copy_buffer_size);
  if (!err) {
    err = sp_io_copy(writer, &reader.base, SP_NULLPTR);
  }
  sp_io_file_reader_close(&reader);

  if (err) {
    sp_fs_atomic_abort(&af);
  }
  else {
    err = sp_fs_atomic_commit(&af, SP_FS_ATOMIC_REPLACE);
  }
  sp_mem_end_scratch(s);
  return err;
}
