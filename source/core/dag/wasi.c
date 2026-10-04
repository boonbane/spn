#include "dag/wasi.h"
#include "dag/dag.h"
#include "paths/paths.h"
#include "str/str.h"
#include "sp/sp_glob.h"

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

typedef struct {
  sp_str_t guest;
  spn_path_t host;
} spn_dag_wasi_dir_t;

struct spn_dag_wasi_t {
  sp_mem_t mem;
  const spn_path_roots_t* roots;
  sp_da(spn_dag_wasi_dir_t) mounts;
  sp_da(spn_path_t) writable;
  sp_ht(u32, sp_str_t) dirs;
  sp_mem_arena_t* call;
  sp_ht(spn_path_t, u8) writes;
  spn_dag_obs_set_t* obs;
};

static sp_str_t wasi_guest_path(spn_dag_wasi_t* w, sp_mem_t mem, u32 fd, const c8* path, u32 path_len) {
  sp_str_t* parent = sp_ht_getp(w->dirs, fd);
  if (!parent) {
    return sp_str_lit("");
  }
  return sp_fmt(mem, "{}/{}", sp_fmt_str(*parent), sp_fmt_str(sp_str(path, path_len))).value;
}

static bool wasi_match(spn_dag_wasi_t* w, sp_mem_t mem, sp_str_t guest, spn_path_t* host) {
  sp_da_for(w->mounts, it) {
    sp_str_t prefix = w->mounts[it].guest;
    if (!sp_str_starts_with(guest, prefix)) {
      continue;
    }
    if (guest.len != prefix.len && guest.data[prefix.len] != '/') {
      continue;
    }
    sp_str_t rest = guest.len == prefix.len ? sp_str_lit("") : sp_str_sub(guest, prefix.len + 1, guest.len - prefix.len - 1);
    *host = spn_path_join(mem, w->mounts[it].host, rest);
    return true;
  }
  return false;
}

static bool wasi_resolve(spn_dag_wasi_t* w, sp_mem_t mem, sp_str_t guest, spn_path_t* host) {
  if (!wasi_match(w, mem, guest, host)) {
    return false;
  }
  *host = spn_path_canonicalize_head(mem, w->roots, *host);
  return true;
}

static void wasi_track_dir(spn_dag_wasi_t* w, u32 fd, sp_str_t guest) {
  sp_ht_insert(w->dirs, fd, sp_str_copy(w->mem, guest));
}

static void wasi_push(spn_dag_wasi_t* w, spn_dag_obs_kind_t kind, spn_path_t host, sp_str_t filter) {
  if (!w->obs) {
    return;
  }
  spn_dag_observe(w->obs, (spn_dag_obs_t) {
    .kind = kind,
    .path = host,
    .filter = filter
  });
}

static void wasi_push_obs(spn_dag_wasi_t* w, spn_dag_obs_kind_t kind, spn_path_t host) {
  wasi_push(w, kind, host, sp_str_lit(""));
}

static bool wasi_written(spn_dag_wasi_t* w, spn_path_t host) {
  return sp_ht_getp(w->writes, host) != SP_NULLPTR;
}

static bool is_writable(spn_dag_wasi_t* w, spn_path_t host) {
  sp_da_for(w->writable, it) {
    if (spn_path_within(w->writable[it], host).within) {
      return true;
    }
  }
  return false;
}

static void wasi_record_write(spn_dag_wasi_t* w, spn_path_t host) {
  if (!wasi_written(w, host)) {
    sp_ht_insert(w->writes, spn_path_copy(sp_mem_arena_as_allocator(w->call), host), (u8)true);
  }
}

static bool wasi_error_absent(u16 error) {
  return error == SPN_WASI_ERRNO_NOENT || error == SPN_WASI_ERRNO_NOTDIR;
}

static void wasi_on_open(spn_dag_wasi_t* w, u32 fd, const c8* path, u32 path_len, u16 error, u32 new_fd, u32 oflags, u64 rights) {
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

static void wasi_on_stat(spn_dag_wasi_t* w, u32 fd, const c8* path, u32 path_len, u16 error) {
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

static void wasi_on_readdir(spn_dag_wasi_t* w, u32 fd, u16 error) {
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

static void wasi_on_mutate(spn_dag_wasi_t* w, u32 fd, const c8* path, u32 path_len, u16 error) {
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

static void wasi_on_renumber(spn_dag_wasi_t* w, u32 from, u32 to, u16 error) {
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
  spn_dag_wasi_t* w = (spn_dag_wasi_t*)wasm_runtime_get_custom_data(instance);
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

spn_err_t spn_dag_wasi_install(void) {
  spn_wasi_hook = wasi_hook;
  return SPN_OK;
}

spn_dag_wasi_t* spn_dag_wasi_new(sp_mem_t mem, const spn_path_roots_t* roots, const spn_dag_wasi_mount_t* mounts, u32 num_mounts, const spn_path_t* writable, u32 num_writable) {
  spn_dag_wasi_t* w = sp_alloc_type(mem, spn_dag_wasi_t);
  w->mem = mem;
  w->roots = roots;
  w->obs = SP_NULLPTR;
  sp_da_init(mem, w->mounts);
  sp_da_init(mem, w->writable);
  sp_ht_init(mem, w->dirs);
  w->call = sp_mem_arena_new(mem);
  sp_ht_init(sp_mem_arena_as_allocator(w->call), w->writes);
  sp_ht_set_fns(w->writes, spn_path_on_hash, spn_path_on_compare);

  sp_for(it, num_mounts) {
    sp_da_push(w->mounts, ((spn_dag_wasi_dir_t) {
      .guest = sp_str_from_cstr(mem, mounts[it].guest),
      .host = spn_path_copy(mem, mounts[it].host)
    }));
    sp_ht_insert(w->dirs, SPN_WASI_PREOPEN_BASE_FD + it, w->mounts[it].guest);
  }

  sp_for(it, num_writable) {
    sp_da_push(w->writable, spn_path_canonicalize(mem, roots, writable[it]));
  }

  return w;
}

void spn_dag_wasi_bind(spn_dag_wasi_t* w, wasm_module_inst_t instance) {
  wasm_runtime_set_custom_data(instance, w);
}

void spn_dag_wasi_begin(spn_dag_wasi_t* w, spn_dag_obs_set_t* obs) {
  sp_mem_arena_clear(w->call);
  sp_ht_init(sp_mem_arena_as_allocator(w->call), w->writes);
  sp_ht_set_fns(w->writes, spn_path_on_hash, spn_path_on_compare);
  w->obs = obs;
}

void spn_dag_wasi_end(spn_dag_wasi_t* w) {
  w->obs = SP_NULLPTR;
}

bool spn_dag_wasi_stray_write(spn_dag_wasi_t* w, spn_path_t* path) {
  sp_ht_for_kv(w->writes, it) {
    if (!is_writable(w, *it.key)) {
      *path = *it.key;
      return true;
    }
  }
  return false;
}

static spn_dag_wasi_t* wasi_of(wasm_module_inst_t instance) {
  return (spn_dag_wasi_t*)wasm_runtime_get_custom_data(instance);
}

bool spn_dag_wasi_resolve(wasm_module_inst_t instance, sp_mem_t mem, sp_str_t guest, spn_path_t* host) {
  return wasi_match(wasi_of(instance), mem, guest, host);
}

bool spn_dag_wasi_writable(wasm_module_inst_t instance, spn_path_t host) {
  spn_dag_wasi_t* w = wasi_of(instance);
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  bool writable = is_writable(w, spn_path_canonicalize_head(s.mem, w->roots, host));
  sp_mem_end_scratch(s);
  return writable;
}

static void wasi_observe_dir(spn_dag_wasi_t* w, spn_path_t dir) {
  wasi_push_obs(w, SPN_DAG_OBS_ENUMERATION, dir);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_fs_for_recursive(s.mem, spn_path_at(w->roots, dir), it) {
    spn_path_t path = spn_path_join(s.mem, dir, it.entry.rel);
    if (wasi_written(w, path)) {
      continue;
    }
    wasi_push_obs(w, it.entry.kind == SP_FS_KIND_DIR ? SPN_DAG_OBS_ENUMERATION : SPN_DAG_OBS_FILE, path);
  }
  sp_mem_end_scratch(s);
}

static void observe(spn_dag_wasi_t* w, spn_path_t host) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  host = spn_path_canonicalize_head(s.mem, w->roots, host);
  if (!wasi_written(w, host)) {
    sp_path_t at = spn_path_at(w->roots, host);
    sp_sys_file_meta_t meta = sp_zero;
    if (sp_sys_get_path_metadata_s(at.dir, at.sub, &meta)) {
      wasi_push_obs(w, SPN_DAG_OBS_ABSENT, host);
    }
    else if (meta.kind == SP_FS_KIND_DIR) {
      wasi_observe_dir(w, host);
    }
    else {
      wasi_push_obs(w, SPN_DAG_OBS_FILE, host);
    }
  }
  sp_mem_end_scratch(s);
}

void spn_dag_wasi_observe_read(wasm_module_inst_t instance, spn_path_t host) {
  spn_dag_wasi_t* w = wasi_of(instance);
  if (!w || !w->obs) {
    return;
  }
  observe(w, host);
}

void spn_dag_wasi_observe_write(wasm_module_inst_t instance, spn_path_t host) {
  spn_dag_wasi_t* w = wasi_of(instance);
  if (!w) {
    return;
  }
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  wasi_record_write(w, spn_path_canonicalize_head(s.mem, w->roots, host));
  sp_mem_end_scratch(s);
}

void spn_dag_wasi_observe_glob(wasm_module_inst_t instance, spn_path_t dir, sp_str_t pattern) {
  spn_dag_wasi_t* w = wasi_of(instance);
  if (!w || !w->obs) {
    return;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_glob_t* glob = sp_glob_new_str(s.mem, pattern);
  if (glob) {
    if (!wasi_written(w, dir)) {
      wasi_push(w, SPN_DAG_OBS_ENUMERATION, dir, pattern);
    }
    sp_fs_for(s.mem, spn_path_at(w->roots, dir), it) {
      if (sp_glob_match(glob, it.entry.name)) {
        observe(w, spn_path_join(s.mem, dir, it.entry.name));
      }
    }
  }
  sp_mem_end_scratch(s);
}
