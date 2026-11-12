#include "external/wasm/wasi.h"
#include "dag/dag.h"
#include "paths/paths.h"
#include "str/str.h"

#define SPN_WASI_OP_PATH_OPEN 0
#define SPN_WASI_OP_PATH_FILESTAT_GET 1
#define SPN_WASI_OP_FD_READDIR 2
#define SPN_WASI_OP_FD_CLOSE 3
#define SPN_WASI_OP_FD_RENUMBER 4
#define SPN_WASI_OP_PATH_CREATE_DIRECTORY 5
#define SPN_WASI_OP_PATH_RENAME 6
#define SPN_WASI_OP_PATH_UNLINK_FILE 7
#define SPN_WASI_OP_PATH_REMOVE_DIRECTORY 8

#define SPN_WASI_ERRNO_NOENT 44
#define SPN_WASI_ERRNO_NOTDIR 54

#define SPN_WASI_OFLAGS_CREAT 0x1
#define SPN_WASI_OFLAGS_DIRECTORY 0x2
#define SPN_WASI_OFLAGS_TRUNC 0x8
#define SPN_WASI_RIGHTS_FD_WRITE 0x40

#define SPN_WASI_PREOPEN_BASE_FD 3

extern void (*spn_wasi_hook)(wasm_exec_env_t exec_env, s32 op, u32 fd,
                             const c8* path, u32 path_len, u16 error,
                             u32 new_fd, u32 oflags, u64 rights);

struct spn_wasi_t {
  sp_mem_t mem;
  const spn_path_roots_t* roots;
  sp_da(spn_wasi_mount_t) mounts;
  sp_ht(u32, sp_str_t) dirs;
  sp_ht(spn_path_t, u8) writes;
  spn_dag_obs_set_t* obs;
};

static sp_str_t wasi_guest_path(spn_wasi_t* w, sp_mem_t mem, u32 fd, const c8* path, u32 path_len) {
  sp_str_t* parent = sp_ht_getp(w->dirs, fd);
  if (!parent) {
    return sp_str_lit("");
  }
  return sp_fmt(mem, "{}/{}", sp_fmt_str(*parent), sp_fmt_str(sp_str(path, path_len))).value;
}

const spn_wasi_mount_t* spn_wasi_find_mount(const spn_wasi_mount_t* mounts, u32 count, sp_str_t guest, sp_str_t* rest) {
  sp_for(it, count) {
    sp_str_t prefix = sp_cstr_as_str(mounts[it].guest);
    if (!sp_str_starts_with(guest, prefix)) {
      continue;
    }
    if (guest.len != prefix.len && guest.data[prefix.len] != '/') {
      continue;
    }
    *rest = guest.len == prefix.len ? sp_str_lit("") : sp_str_sub(guest, prefix.len + 1, guest.len - prefix.len - 1);
    return &mounts[it];
  }
  return SP_NULLPTR;
}

static bool wasi_match(spn_wasi_t* w, sp_mem_t mem, sp_str_t guest, spn_path_t* host) {
  sp_str_t rest = sp_zero;
  const spn_wasi_mount_t* mount = spn_wasi_find_mount(w->mounts, sp_da_size(w->mounts), guest, &rest);
  if (!mount || sp_fs_is_absolute(rest)) {
    return false;
  }
  *host = spn_path_join(mem, mount->host, rest);
  return true;
}

static bool wasi_resolve(spn_wasi_t* w, sp_mem_t mem, sp_str_t guest, spn_path_t* host) {
  if (!wasi_match(w, mem, guest, host)) {
    return false;
  }
  *host = spn_path_canonicalize_head(mem, w->roots, *host);
  return true;
}

static void wasi_track_dir(spn_wasi_t* w, u32 fd, sp_str_t guest) {
  sp_ht_insert(w->dirs, fd, sp_str_copy(w->mem, guest));
}

static void wasi_push(spn_wasi_t* w, spn_dag_obs_kind_t kind, spn_path_t host, sp_str_t filter) {
  if (!w->obs) {
    return;
  }
  spn_dag_observe(w->obs, (spn_dag_obs_t) {
    .kind = kind,
    .path = host,
    .filter = filter
  });
}

static void wasi_push_obs(spn_wasi_t* w, spn_dag_obs_kind_t kind, spn_path_t host) {
  wasi_push(w, kind, host, sp_str_lit(""));
}

static bool wasi_written(spn_wasi_t* w, spn_path_t host) {
  return sp_ht_getp(w->writes, host) != SP_NULLPTR;
}

static void wasi_record_write(spn_wasi_t* w, spn_path_t host) {
  if (!wasi_written(w, host)) {
    sp_ht_insert(w->writes, spn_path_copy(w->mem, host), (u8)true);
  }
}

static bool wasi_error_absent(u16 error) {
  return error == SPN_WASI_ERRNO_NOENT || error == SPN_WASI_ERRNO_NOTDIR;
}

static void wasi_on_open(spn_wasi_t* w, u32 fd, const c8* path, u32 path_len, u16 error, u32 new_fd, u32 oflags, u64 rights) {
  if (error && !wasi_error_absent(error)) {
    return;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_str_t guest = wasi_guest_path(w, s.mem, fd, path, path_len);
  spn_path_t host = sp_zero;
  if (!wasi_resolve(w, s.mem, guest, &host)) {
    sp_mem_end_scratch(s);
    return;
  }

  if (error) {
    wasi_push_obs(w, SPN_DAG_OBS_ABSENT, host);
    sp_mem_end_scratch(s);
    return;
  }

  wasi_track_dir(w, new_fd, guest);

  bool write = (oflags & (SPN_WASI_OFLAGS_CREAT | SPN_WASI_OFLAGS_TRUNC)) || (rights & SPN_WASI_RIGHTS_FD_WRITE);
  if (write) {
    wasi_record_write(w, host);
  }
  else if (!(oflags & SPN_WASI_OFLAGS_DIRECTORY) && !wasi_written(w, host)) {
    wasi_push_obs(w, SPN_DAG_OBS_FILE, host);
  }

  sp_mem_end_scratch(s);
}

static void wasi_on_stat(spn_wasi_t* w, u32 fd, const c8* path, u32 path_len, u16 error) {
  if (error && !wasi_error_absent(error)) {
    return;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_path_t host = sp_zero;
  if (wasi_resolve(w, s.mem, wasi_guest_path(w, s.mem, fd, path, path_len), &host) && !wasi_written(w, host)) {
    wasi_push_obs(w, error ? SPN_DAG_OBS_ABSENT : SPN_DAG_OBS_FILE, host);
  }
  sp_mem_end_scratch(s);
}

static void wasi_on_readdir(spn_wasi_t* w, u32 fd, u16 error) {
  if (error) {
    return;
  }

  sp_str_t* guest = sp_ht_getp(w->dirs, fd);
  if (!guest) {
    return;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_path_t host = sp_zero;
  if (wasi_resolve(w, s.mem, *guest, &host) && !wasi_written(w, host)) {
    wasi_push_obs(w, SPN_DAG_OBS_ENUMERATION, host);
  }
  sp_mem_end_scratch(s);
}

static void wasi_on_mutate(spn_wasi_t* w, u32 fd, const c8* path, u32 path_len, u16 error) {
  if (error) {
    return;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_path_t host = sp_zero;
  if (wasi_resolve(w, s.mem, wasi_guest_path(w, s.mem, fd, path, path_len), &host)) {
    wasi_record_write(w, host);
  }
  sp_mem_end_scratch(s);
}

static void wasi_on_renumber(spn_wasi_t* w, u32 from, u32 to, u16 error) {
  if (error) {
    return;
  }

  sp_str_t* guest = sp_ht_getp(w->dirs, from);
  if (guest) {
    wasi_track_dir(w, to, *guest);
    sp_ht_erase(w->dirs, from);
  }
  else {
    sp_ht_erase(w->dirs, to);
  }
}

static void wasi_hook(wasm_exec_env_t exec_env, s32 op, u32 fd, const c8* path, u32 path_len, u16 error, u32 new_fd, u32 oflags, u64 rights) {
  wasm_module_inst_t instance = wasm_runtime_get_module_inst(exec_env);
  spn_wasi_t* w = (spn_wasi_t*)wasm_runtime_get_custom_data(instance);
  if (!w) {
    return;
  }

  switch (op) {
    case SPN_WASI_OP_PATH_OPEN: {
      wasi_on_open(w, fd, path, path_len, error, new_fd, oflags, rights);
      break;
    }
    case SPN_WASI_OP_PATH_FILESTAT_GET: {
      wasi_on_stat(w, fd, path, path_len, error);
      break;
    }
    case SPN_WASI_OP_FD_READDIR: {
      wasi_on_readdir(w, fd, error);
      break;
    }
    case SPN_WASI_OP_FD_CLOSE: {
      if (!error) {
        sp_ht_erase(w->dirs, fd);
      }
      break;
    }
    case SPN_WASI_OP_FD_RENUMBER: {
      wasi_on_renumber(w, fd, new_fd, error);
      break;
    }
    case SPN_WASI_OP_PATH_CREATE_DIRECTORY:
    case SPN_WASI_OP_PATH_RENAME:
    case SPN_WASI_OP_PATH_UNLINK_FILE:
    case SPN_WASI_OP_PATH_REMOVE_DIRECTORY: {
      wasi_on_mutate(w, fd, path, path_len, error);
      break;
    }
  }
}

spn_err_t spn_wasi_install(void) {
  spn_wasi_hook = wasi_hook;
  return SPN_OK;
}

spn_wasi_t* spn_wasi_new(sp_mem_t mem, const spn_path_roots_t* roots, const spn_wasi_mount_t* mounts, u32 count, spn_dag_obs_set_t* obs) {
  spn_wasi_t* w = sp_alloc_type(mem, spn_wasi_t);
  w->mem = mem;
  w->roots = roots;
  w->obs = obs;
  sp_da_init(mem, w->mounts);
  sp_ht_init(mem, w->dirs);
  sp_ht_init(mem, w->writes);
  sp_ht_set_fns(w->writes, spn_path_on_hash, spn_path_on_compare);

  sp_for(it, count) {
    sp_da_push(w->mounts, ((spn_wasi_mount_t) {
      .tree = mounts[it].tree,
      .guest = sp_cstr_copy(mem, mounts[it].guest),
      .host = spn_path_canonicalize_head(mem, roots, mounts[it].host),
      .writable = mounts[it].writable,
    }));
    sp_ht_insert(w->dirs, SPN_WASI_PREOPEN_BASE_FD + it, sp_cstr_as_str(w->mounts[it].guest));
  }

  return w;
}

void spn_wasi_bind(spn_wasi_t* w, wasm_module_inst_t instance) {
  wasm_runtime_set_custom_data(instance, w);
}

static spn_wasi_t* wasi_of(wasm_module_inst_t instance) {
  return (spn_wasi_t*)wasm_runtime_get_custom_data(instance);
}

bool spn_wasi_resolve(wasm_module_inst_t instance, sp_mem_t mem, sp_str_t guest, spn_path_t* host) {
  return wasi_match(wasi_of(instance), mem, guest, host);
}

void spn_wasi_observe_write(wasm_module_inst_t instance, spn_path_t host) {
  spn_wasi_t* w = wasi_of(instance);
  if (!w) {
    return;
  }
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  wasi_record_write(w, spn_path_canonicalize_head(s.mem, w->roots, host));
  sp_mem_end_scratch(s);
}

bool spn_wasi_stray_write(spn_wasi_t* w, spn_path_t* path) {
  sp_ht_for_kv(w->writes, it) {
    bool allowed = false;
    sp_da_for(w->mounts, mt) {
      if (w->mounts[mt].writable && spn_path_within(w->mounts[mt].host, *it.key).within) {
        allowed = true;
      }
    }
    if (!allowed) {
      *path = *it.key;
      return true;
    }
  }
  return false;
}
