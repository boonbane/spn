#include "core/core.h"
#include "fs/fs.h"
#include "io/io.h"
#include "paths/paths.h"
#include "sp/sp_glob.h"

spn_fs_it_t spn_fs_it_new(sp_mem_t mem, const spn_path_roots_t* roots, sp_da(spn_path_t) owned, spn_path_t dir) {
  return (spn_fs_it_t) {
    .fs = sp_fs_it_new_at(mem, spn_path_at(roots, dir), 0),
    .dir = dir,
    .owned = owned,
  };
}

bool spn_fs_owned(sp_da(spn_path_t) owned, spn_path_t dir, sp_str_t rel) {
  sp_da_for(owned, it) {
    spn_path_rel_t within = spn_path_within(dir, owned[it]);
    if (within.within && sp_str_equal(within.sub, rel)) {
      return true;
    }
  }
  return false;
}

bool spn_fs_it_next(spn_fs_it_t* it) {
  while (sp_fs_it_next(&it->fs)) {
    if (!spn_fs_owned(it->owned, it->dir, it->fs.entry.rel)) {
      return true;
    }
  }
  return false;
}

bool spn_fs_it_walk(spn_fs_it_t* it) {
  while (sp_fs_it_next(&it->fs)) {
    if (it->fs.yield == SP_FS_IT_LEAVE || spn_fs_owned(it->owned, it->dir, it->fs.entry.rel)) {
      continue;
    }
    if (it->fs.entry.kind == SP_FS_KIND_DIR) {
      it->fs.err = sp_fs_it_enter(&it->fs);
    }
    return !it->fs.err;
  }
  return false;
}

void spn_fs_it_deinit(spn_fs_it_t* it) {
  sp_fs_it_deinit(&it->fs);
}

spn_err_t spn_fs_update_file(sp_path_t from, sp_path_t to) {
  sp_sys_file_meta_t source = sp_zero;
  if (sp_sys_get_path_metadata_s(from.dir, from.sub, &source)) {
    return SPN_ERROR;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();

  sp_sys_file_meta_t dest = sp_zero;
  sp_sys_get_path_metadata_s(to.dir, to.sub, &dest);

  bool matches = false;
  if (dest.kind == SP_FS_KIND_FILE && dest.size == source.size) {
    sp_mem_slice_t content = sp_zero;
    sp_mem_slice_t existing = sp_zero;
    matches = !sp_io_read_file_slice(s.mem, from, &content)
      && !sp_io_read_file_slice(s.mem, to, &existing)
      && existing.len == content.len
      && sp_mem_is_equal(existing.data, content.data, content.len);
  }

  spn_err_t err = SPN_OK;
  if (!matches) {
    sp_fs_create_parent_at(to);
    if (sp_fs_copy_file_at(from, to, SP_FS_ATOMIC_REPLACE)) {
      err = SPN_ERROR;
    }
  }

  sp_mem_end_scratch(s);
  return err;
}

spn_err_t spn_fs_update_tree(const spn_path_roots_t* roots, sp_da(spn_path_t) owned, spn_path_t from, sp_path_t to) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_fs_it_t walk = spn_fs_it_new(s.mem, roots, owned, from);

  sp_sys_fd_t fd = SP_SYS_INVALID_FD;
  spn_err_t err = SPN_OK;
  if (walk.fs.err || sp_fs_create_dir_at(to) || sp_fs_open_dir_at(to, &fd)) {
    err = SPN_ERROR;
  }

  while (!err && spn_fs_it_walk(&walk)) {
    sp_path_t dest = sp_path(fd, walk.fs.entry.rel);
    switch (walk.fs.entry.kind) {
      case SP_FS_KIND_DIR: {
        err = sp_fs_create_dir_at(dest) ? SPN_ERROR : SPN_OK;
        break;
      }
      case SP_FS_KIND_FILE: {
        err = spn_fs_update_file(walk.fs.at, dest);
        break;
      }
      case SP_FS_KIND_SYMLINK:
      case SP_FS_KIND_NONE: {
        err = sp_fs_copy_at(walk.fs.at, dest, SP_FS_ATOMIC_REPLACE) ? SPN_ERROR : SPN_OK;
        break;
      }
    }
  }
  spn_fs_it_deinit(&walk);
  if (walk.fs.err) {
    err = SPN_ERROR;
  }

  if (fd != SP_SYS_INVALID_FD) {
    sp_sys_close(fd);
  }
  sp_mem_end_scratch(s);
  return err;
}

spn_err_t spn_fs_update_glob(const spn_path_roots_t* roots, sp_da(spn_path_t) owned, spn_path_t from, sp_path_t to) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = SPN_OK;

  spn_path_t dir = spn_path_parent(from);
  sp_glob_t* glob = sp_glob_new_str(s.mem, sp_fs_get_name(from.sub));
  spn_fs_it_t walk = spn_fs_it_new(s.mem, roots, owned, dir);
  if (!glob || walk.fs.err || sp_fs_create_dir_at(to)) {
    err = SPN_ERROR;
  }

  while (!err && spn_fs_it_next(&walk)) {
    sp_fs_entry_t* entry = &walk.fs.entry;
    if (!sp_glob_match(glob, entry->name)) {
      continue;
    }

    sp_path_t dest = sp_path_join(s.mem, to, entry->name);
    sp_fs_kind_t kind = entry->kind == SP_FS_KIND_SYMLINK ? sp_fs_get_target_kind_at(walk.fs.at) : entry->kind;
    switch (kind) {
      case SP_FS_KIND_FILE: {
        err = spn_fs_update_file(walk.fs.at, dest);
        break;
      }
      case SP_FS_KIND_DIR: {
        err = spn_fs_update_tree(roots, owned, spn_path_join(s.mem, dir, entry->name), dest);
        break;
      }
      case SP_FS_KIND_SYMLINK:
      case SP_FS_KIND_NONE: {
        err = SPN_ERROR;
        break;
      }
    }
  }
  spn_fs_it_deinit(&walk);
  if (walk.fs.err) {
    err = SPN_ERROR;
  }

  sp_mem_end_scratch(s);
  return err;
}

void spn_wake_ring(spn_wake_t* wake) {
  if (!wake->fn) {
    return;
  }
  if (sp_atomic_u32_cas(&wake->signaled, 0, 1, SP_ATOMIC_SEQ_CST)) {
    wake->fn(wake->data);
  }
}

void spn_wake_pulse(spn_wake_t* wake) {
  if (!wake->fn) {
    return;
  }
  sp_atomic_u32_store(&wake->signaled, 1, SP_ATOMIC_SEQ_CST);
  wake->fn(wake->data);
}

void spn_wake_rearm(spn_wake_t* wake) {
  sp_atomic_u32_store(&wake->signaled, 0, SP_ATOMIC_SEQ_CST);
}
