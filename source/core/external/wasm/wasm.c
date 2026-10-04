#include "sp.h"
#include "external/wasm/wasm.h"
#include "spn/errors.h"

#include "spn.h"
#include "ctx/types.h"
#include "event/types.h"
#include "event/event.h"
#include "unit/package.h"
#include "unit/types.h"
#include "wasm_export.h"

#include "external/wasm/abi.h"
#include "dag/dag.h"
#include "dag/wasi.h"
#include "hash/digest/digest.h"
#include "paths/paths.h"
#include "str/str.h"

#define SPN_WASM_STACK_SIZE (8 * 1024 * 1024)
#define SPN_WASM_HEAP_SIZE  (16 * 1024 * 1024)
#define SPN_WASM_EXPORT_MAX 128

#define SPN_WASM_NO_ENV SP_NULL
#define SPN_WASM_NO_DIRS SP_NULL
#define SPN_WASM_NO_ARGS SP_NULL

spn_err_t spn_wasm_init() {
  static bool initialized = false;
  if (initialized) return SPN_OK;
  initialized = true;

  if (!wasm_runtime_init()) {
    return SPN_ERR_WASM_INIT_FAILED;
  }

  if (wasm_runtime_is_running_mode_supported(Mode_Fast_JIT)) {
    if (!wasm_runtime_set_default_running_mode(Mode_Fast_JIT)) {
      return SPN_ERR_WASM_INIT_FAILED;
    }
  }

  if (!spn_wasm_register_api()) {
    return SPN_ERR_WASM_REGISTER_FAILED;
  }

  return spn_dag_wasi_install();
}

// WAMR mprotects guard pages at the bottom of any thread that runs a script;
// a thread that exits without destroying its env leaves those pages PROT_NONE
// on a stack glibc will hand to the next pthread, which then faults on it
void spn_wasm_thread_exit(void) {
  if (wasm_runtime_thread_env_inited()) {
    wasm_runtime_destroy_thread_env();
  }
}

static spn_err_t script_fail(spn_pkg_unit_t* unit, spn_err_t err, spn_err_wasm_t wasm) {
  wasm.path = spn_path_copy(spn.mem, wasm.path);
  wasm.error = sp_str_copy(spn.mem, wasm.error);
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_ERR,
    .pkg = unit->info->name,
    .err = { .kind = err, .wasm = wasm },
  });
  return err;
}

static const c8* preopen(const c8* guest, sp_str_t host) {
  return sp_fmt_mem_cstr(spn.mem, "{}::{}", sp_fmt_cstr(guest), sp_fmt_str(host));
}

static spn_err_t script_open(spn_wasm_script_t* script, spn_pkg_unit_t* unit) {
  if (!wasm_runtime_init_thread_env()) {
    return script_fail(unit, SPN_ERR_WASM_THREAD_ENV_FAILED, (spn_err_wasm_t) { .path = script->path });
  }

  const spn_path_roots_t* roots = &unit->session->ctx->roots;
  sp_str_t blob = sp_zero;
  if (sp_io_read_file_at(spn.mem, spn_path_at(roots, script->path), &blob)) {
    return script_fail(unit, SPN_ERR_WASM_READ_FAILED, (spn_err_wasm_t) { .path = script->path });
  }

  c8 error [128] = sp_zero;
  script->module = wasm_runtime_load((u8*)blob.data, blob.len, error, sizeof(error));
  if (!script->module) {
    return script_fail(unit, SPN_ERR_WASM_MODULE_LOAD_FAILED, (spn_err_wasm_t) {
      .path = script->path,
      .error = sp_cstr_as_str(error),
    });
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_hash_t identity [] = {
    spn_digest_hash_str(spn_path_str(roots, s.mem, unit->paths.work)),
    spn_digest_hash_str(spn_path_str(roots, s.mem, script->path)),
  };
  sp_str_t name = sp_fmt(s.mem, "script/{:0>16x}", sp_fmt_uint(sp_hash_combine(identity, sp_carr_len(identity)))).value;
  spn_path_t root = spn_path_join(spn.mem, spn_path_from_root(SPN_PATH_ROOT_CACHE), name);
  sp_mem_end_scratch(s);
  spn_path_t store = spn_path_join(spn.mem, root, sp_str_lit("store"));
  script->private = (spn_wasm_private_t) {
    .root = root,
    .store = store,
    .work = spn_path_join(spn.mem, root, sp_str_lit("work")),
    .lib = spn_path_join(spn.mem, store, sp_str_lit("lib")),
    .bin = spn_path_join(spn.mem, store, sp_str_lit("bin")),
    .share = spn_path_join(spn.mem, store, sp_str_lit("share")),
  };
  sp_carr_for(script->private.writable, it) {
    sp_fs_create_dir_at(spn_path_at(roots, script->private.writable[it]));
  }
  spn_dag_wasi_mount_t mounts [] = {
    { .guest = "/work",     .host = script->private.work, .private = true },
    { .guest = "/source",   .host = unit->paths.roots.source },
    { .guest = "/manifest", .host = unit->paths.roots.recipe },
    { .guest = "/store",    .host = script->private.store, .private = true },
  };
  sp_carr_for(mounts, it) {
    script->preopens.array[it] = preopen(mounts[it].guest, spn_path_str(roots, spn.mem, mounts[it].host));
  }
  wasm_runtime_set_wasi_args(
    script->module,
    SPN_WASM_NO_DIRS, SPN_WASM_NO_DIRS,
    script->preopens.array, sp_carr_len(script->preopens.array),
    SPN_WASM_NO_ENV, SPN_WASM_NO_ENV,
    SPN_WASM_NO_ARGS, SPN_WASM_NO_ARGS
  );

  script->instance = wasm_runtime_instantiate(script->module, SPN_WASM_STACK_SIZE, SPN_WASM_HEAP_SIZE, error, sizeof(error));
  if (!script->instance) {
    wasm_runtime_unload(script->module);
    script->module = SP_NULLPTR;
    return script_fail(unit, SPN_ERR_WASM_MODULE_INSTANCE_FAILED, (spn_err_wasm_t) {
      .path = script->path,
      .error = sp_cstr_as_str(error),
    });
  }

  script->env = wasm_runtime_create_exec_env(script->instance, SPN_WASM_STACK_SIZE);
  if (!script->env) {
    wasm_runtime_deinstantiate(script->instance);
    script->instance = SP_NULLPTR;
    wasm_runtime_unload(script->module);
    script->module = SP_NULLPTR;
    return script_fail(unit, SPN_ERR_WASM_CTX_FAILED, (spn_err_wasm_t) { .path = script->path });
  }

  script->handles = sp_alloc_type(spn.mem, spn_wasm_handles_t);
  sp_ht_init(spn.mem, script->handles->map);
  script->handles->ctx = sp_ptr_cast(spn_t*, unit);
  script->ctx = spn_wasm_add_handle(script->handles, unit, SPN_ABI_KIND_CTX);
  wasm_runtime_set_user_data(script->env, script->handles);

  script->wasi = spn_dag_wasi_new(spn.mem, roots, unit->session->paths.build, mounts, sp_carr_len(mounts), script->private.writable, sp_carr_len(script->private.writable));
  spn_dag_wasi_bind(script->wasi, script->instance);

  return SPN_OK;
}

spn_err_t spn_wasm_script_open(spn_wasm_script_t* script, spn_pkg_unit_t* unit) {
  SP_ASSERT(script->state != SPN_WASM_SCRIPT_NONE);

  spn_err_t err = SPN_OK;

  sp_mutex_lock(&script->mutex);
  switch (script->state) {
    case SPN_WASM_SCRIPT_NONE:
    case SPN_WASM_SCRIPT_OPEN: {
      break;
    }
    case SPN_WASM_SCRIPT_FAILED: {
      err = script->err;
      break;
    }
    case SPN_WASM_SCRIPT_CLOSED: {
      err = script_open(script, unit);
      script->err = err;
      script->state = err ? SPN_WASM_SCRIPT_FAILED : SPN_WASM_SCRIPT_OPEN;
      break;
    }
  }
  sp_mutex_unlock(&script->mutex);

  return err;
}

void spn_wasm_script_close(spn_wasm_script_t* script, spn_pkg_unit_t* unit) {
  sp_mutex_lock(&script->mutex);
  if (script->state == SPN_WASM_SCRIPT_OPEN) {
    wasm_runtime_destroy_exec_env(script->env);
    script->env = SP_NULLPTR;
    wasm_runtime_deinstantiate(script->instance);
    script->instance = SP_NULLPTR;
    wasm_runtime_unload(script->module);
    script->module = SP_NULLPTR;
    sp_fs_remove_dir_at(spn_path_at(&unit->session->ctx->roots, script->private.root));
    script->state = SPN_WASM_SCRIPT_CLOSED;
  }
  sp_mutex_unlock(&script->mutex);
}

static void export_name(sp_str_t name, c8* buffer) {
  sp_str_copy_to(name, buffer, SPN_WASM_EXPORT_MAX - 1);
}

bool spn_wasm_script_exports(spn_wasm_script_t* script, sp_str_t name) {
  bool exported = false;

  sp_mutex_lock(&script->mutex);
  if (script->state == SPN_WASM_SCRIPT_OPEN) {
    c8 buffer [SPN_WASM_EXPORT_MAX] = sp_zero;
    export_name(name, buffer);
    exported = wasm_runtime_lookup_function(script->instance, buffer) != SP_NULLPTR;
  }
  sp_mutex_unlock(&script->mutex);

  return exported;
}

static spn_err_t script_call_invoke(spn_wasm_script_t* script, spn_pkg_unit_t* unit, wasm_function_inst_t fn, spn_abi_kind_t kind, void* arg) {
  wasm_val_t args [2] = {
    { .kind = WASM_I32, .of.i32 = (s32)script->ctx },
  };
  u32 num_args = 1;
  u32 token = 0;
  if (kind != SPN_ABI_KIND_NONE) {
    token = spn_wasm_add_handle(script->handles, arg, kind);
    args[1] = (wasm_val_t) { .kind = WASM_I32, .of.i32 = (s32)token };
    num_args = 2;
  }

  wasm_val_t results [1] = sp_zero;
  bool called = wasm_runtime_call_wasm_a(script->env, fn, 1, results, num_args, args);
  if (token) {
    spn_wasm_remove_handle(script->handles, token);
  }

  if (!called) {
    return script_fail(unit, SPN_ERR_WASM_MODULE_CALL_FAILED, (spn_err_wasm_t) {
      .path = script->path,
      .error = sp_cstr_as_str(wasm_runtime_get_exception(script->instance)),
    });
  }
  if (results[0].of.i32) {
    return script_fail(unit, SPN_ERR_WASM_SCRIPT_ERROR, (spn_err_wasm_t) {
      .path = script->path,
      .rc = results[0].of.i32,
    });
  }
  return SPN_OK;
}

static spn_err_t fs_fail(spn_pkg_unit_t* unit, spn_err_t err, spn_path_t path) {
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_ERR,
    .pkg = unit->info->name,
    .err = { .kind = err, .fs = { .path = spn_path_copy(spn.mem, path) } },
  });
  return err;
}

static spn_err_t create_dir(spn_pkg_unit_t* unit, spn_path_t dir) {
  if (sp_fs_create_dir_at(spn_path_at(&unit->session->ctx->roots, dir))) {
    return fs_fail(unit, SPN_ERR_FS_CREATE_DIR, dir);
  }
  return SPN_OK;
}

static spn_path_t private_path(sp_mem_t mem, spn_wasm_script_t* script, const spn_user_output_t* output) {
  spn_path_t dir = sp_zero;
  switch (output->dir) {
    case SPN_DIR_WORK:     dir = script->private.work; break;
    case SPN_DIR_LIB:      dir = script->private.lib; break;
    case SPN_DIR_BIN:      dir = script->private.bin; break;
    case SPN_DIR_SHARE:    dir = script->private.share; break;
    case SPN_DIR_NONE:
    case SPN_DIR_CACHE:
    case SPN_DIR_STORE:
    case SPN_DIR_INCLUDE:
    case SPN_DIR_SOURCE:
    case SPN_DIR_PROJECT:
    case SPN_DIR_MANIFEST: sp_unreachable_case();
  }
  return spn_path_join(mem, dir, output->sub);
}

static spn_err_t script_call_ex(spn_wasm_script_t* script, spn_pkg_unit_t* unit, sp_str_t name, spn_abi_kind_t kind, void* arg, spn_dag_obs_set_t* obs, spn_wasm_output_t* outputs, u32 num_outputs) {
  if (!wasm_runtime_init_thread_env()) {
    return script_fail(unit, SPN_ERR_WASM_THREAD_ENV_FAILED, (spn_err_wasm_t) { .path = script->path });
  }

  c8 buffer [SPN_WASM_EXPORT_MAX] = sp_zero;
  export_name(name, buffer);

  const spn_path_roots_t* roots = &unit->session->ctx->roots;
  spn_path_t mounted [] = { script->private.work, script->private.store };

  sp_mutex_lock(&script->mutex);
  SP_ASSERT(script->state == SPN_WASM_SCRIPT_OPEN);
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = SPN_OK;

  wasm_function_inst_t fn = wasm_runtime_lookup_function(script->instance, buffer);
  if (!fn) {
    err = script_fail(unit, SPN_ERR_WASM_EXPORT_NOT_FOUND, (spn_err_wasm_t) {
      .path = script->path,
      .error = name,
    });
    goto done;
  }

  sp_carr_for(mounted, it) {
    sp_fs_it_t entries = sp_fs_it_new_at(s.mem, spn_path_at(roots, mounted[it]), 0);
    sp_err_t removed = SP_OK;
    while (!removed && sp_fs_it_next(&entries)) {
      removed = entries.entry.kind == SP_FS_KIND_DIR ? sp_fs_remove_dir_at(entries.at) : sp_fs_remove_file_at(entries.at);
    }
    sp_fs_it_deinit(&entries);
    if (removed || entries.err) {
      err = fs_fail(unit, SPN_ERR_FS_REMOVE, mounted[it]);
      goto done;
    }
  }
  sp_carr_for(script->private.writable, it) {
    err = create_dir(unit, script->private.writable[it]);
    if (err) {
      goto done;
    }
  }
  sp_for(it, num_outputs) {
    err = create_dir(unit, spn_path_parent(private_path(s.mem, script, outputs[it].declared)));
    if (err) {
      goto done;
    }
  }

  spn_dag_wasi_begin(script->wasi, obs);
  spn_wasm_script_t* previous = unit->wasm.active;
  unit->wasm.active = script;
  err = script_call_invoke(script, unit, fn, kind, arg);
  unit->wasm.active = previous;
  spn_dag_wasi_end(script->wasi);
  if (err) {
    goto done;
  }

  spn_path_t stray = sp_zero;
  if (spn_dag_wasi_stray_write(script->wasi, &stray)) {
    err = fs_fail(unit, SPN_ERR_WASM_WRITE_OUTSIDE, stray);
    goto done;
  }

  spn_path_t inside = sp_zero;
  if (spn_dag_wasi_build_read(script->wasi, &inside)) {
    err = fs_fail(unit, SPN_ERR_WASM_READ_BUILD, inside);
    goto done;
  }

  sp_for(it, num_outputs) {
    spn_wasm_output_t* output = &outputs[it];
    sp_path_t from = spn_path_at(roots, private_path(s.mem, script, output->declared));
    sp_path_t to = spn_path_at(roots, output->to);
    switch (output->declared->kind) {
      case SPN_DAG_ARTIFACT_KIND_FILE:  output->err = sp_fs_copy_file_at(from, to, SP_FS_ATOMIC_REPLACE); break;
      case SPN_DAG_ARTIFACT_KIND_TREE:  output->err = sp_fs_copy_tree_at(from, to, SP_FS_ATOMIC_REPLACE); break;
      case SPN_DAG_ARTIFACT_KIND_VALUE: sp_unreachable_case();
    }
  }

done:
  sp_mem_end_scratch(s);
  sp_mutex_unlock(&script->mutex);
  return err;
}

spn_err_t spn_wasm_script_call(spn_wasm_script_t* script, spn_pkg_unit_t* unit, sp_str_t name, spn_abi_kind_t kind, void* arg) {
  return script_call_ex(script, unit, name, kind, arg, SP_NULLPTR, SP_NULLPTR, 0);
}

bool spn_wasm_trap_active(spn_pkg_unit_t* unit, sp_str_t message) {
  spn_wasm_script_t* script = unit->wasm.active;
  if (!script) {
    return false;
  }
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  wasm_runtime_set_exception(script->instance, sp_str_to_cstr(s.mem, message));
  sp_mem_end_scratch(s);
  return true;
}

spn_err_t spn_wasm_call_export_ex(spn_pkg_unit_t* unit, sp_str_t name, spn_abi_kind_t kind, void* arg, spn_dag_obs_set_t* obs, spn_wasm_output_t* outputs, u32 num_outputs) {
  spn_wasm_script_t* script = SP_NULLPTR;
  spn_wasm_script_t* candidates [] = { &unit->wasm.build, &unit->wasm.configure };
  sp_carr_for(candidates, it) {
    spn_wasm_script_t* candidate = candidates[it];
    if (candidate->state == SPN_WASM_SCRIPT_NONE) continue;

    spn_try(spn_wasm_script_open(candidate, unit));
    if (spn_wasm_script_exports(candidate, name)) {
      script = candidate;
      break;
    }
  }

  if (!script) {
    if (unit->wasm.build.state != SPN_WASM_SCRIPT_NONE) {
      return script_fail(unit, SPN_ERR_WASM_EXPORT_NOT_FOUND, (spn_err_wasm_t) {
        .path = unit->wasm.build.path,
        .error = name,
      });
    }
    if (unit->wasm.configure.state != SPN_WASM_SCRIPT_NONE) {
      return script_fail(unit, SPN_ERR_WASM_EXPORT_NOT_FOUND, (spn_err_wasm_t) {
        .path = unit->wasm.configure.path,
        .error = name,
      });
    }
    return script_fail(unit, SPN_ERR_WASM_NO_SCRIPT, (spn_err_wasm_t) { .error = name });
  }

  return script_call_ex(script, unit, name, kind, arg, obs, outputs, num_outputs);
}
