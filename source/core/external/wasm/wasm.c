#include "sp.h"
#include "external/wasm/wasm.h"
#include "spn/errors.h"

#include "spn.h"
#include "ctx/types.h"
#include "event/types.h"
#include "event/event.h"
#include "unit/types.h"
#include "wasm_export.h"

#include "external/wasm/abi.h"
#include "dag/wasi.h"
#include "paths/paths.h"
#include "str/str.h"

#define SPN_WASM_STACK_SIZE (8 * 1024 * 1024)
#define SPN_WASM_HEAP_SIZE  (16 * 1024 * 1024)
#define SPN_WASM_EXPORT_MAX 128

#define SPN_WASM_NO_ENV SP_NULL
#define SPN_WASM_NO_DIRS SP_NULL
#define SPN_WASM_NO_ARGS SP_NULL

static SP_THREAD_LOCAL spn_wasm_script_t* active;

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

static spn_err_t fail(spn_pkg_unit_t* unit, spn_err_t err, spn_err_wasm_t wasm) {
  wasm.error = sp_str_copy(spn.events->mem, wasm.error);
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_ERR,
    .pkg = unit->eval.manifest->name.str,
    .err = { .kind = err, .wasm = wasm },
  });
  return err;
}

spn_err_t spn_wasm_script_open(spn_pkg_unit_t* unit, spn_path_t module, spn_dag_obs_set_t* obs, spn_wasm_script_t* script) {
  *script = (spn_wasm_script_t) { .unit = unit, .path = module };
  if (!wasm_runtime_init_thread_env()) {
    return fail(unit, SPN_ERR_WASM_THREAD_ENV_FAILED, (spn_err_wasm_t) { .path = module });
  }

  script->arena = sp_mem_arena_new(sp_mem_os_new());
  sp_mem_t mem = sp_mem_arena_as_allocator(script->arena);

  const spn_path_roots_t* roots = &unit->session->ctx->roots;
  sp_str_t blob = sp_zero;
  if (sp_io_read_file_at(mem, spn_path_at(roots, module), &blob)) {
    sp_mem_arena_destroy(script->arena);
    return fail(unit, SPN_ERR_WASM_READ_FAILED, (spn_err_wasm_t) { .path = module });
  }

  c8 error [128] = sp_zero;
  script->module = wasm_runtime_load((u8*)blob.data, blob.len, error, sizeof(error));
  if (!script->module) {
    sp_mem_arena_destroy(script->arena);
    return fail(unit, SPN_ERR_WASM_MODULE_LOAD_FAILED, (spn_err_wasm_t) {
      .path = module,
      .error = sp_cstr_as_str(error),
    });
  }

  spn_path_t dirs [] = { unit->paths.work, unit->paths.lib, unit->paths.bin, unit->paths.vendor };
  sp_carr_for(dirs, it) {
    sp_fs_create_dir_at(spn_path_at(roots, dirs[it]));
  }
  spn_dag_wasi_mount_t mounts [] = {
    { .guest = "/work",     .host = unit->paths.work },
    { .guest = "/source",   .host = unit->paths.roots.source },
    { .guest = "/manifest", .host = unit->paths.roots.recipe },
    { .guest = "/store",    .host = unit->paths.store },
  };
  const c8* preopens [sp_carr_len(mounts)] = sp_zero;
  sp_carr_for(mounts, it) {
    preopens[it] = sp_fmt_mem_cstr(mem, "{}::{}", sp_fmt_cstr(mounts[it].guest), sp_fmt_str(spn_path_str(roots, mem, mounts[it].host)));
  }
  wasm_runtime_set_wasi_args(
    script->module,
    SPN_WASM_NO_DIRS, SPN_WASM_NO_DIRS,
    preopens, sp_carr_len(preopens),
    SPN_WASM_NO_ENV, SPN_WASM_NO_ENV,
    SPN_WASM_NO_ARGS, SPN_WASM_NO_ARGS
  );

  script->instance = wasm_runtime_instantiate(script->module, SPN_WASM_STACK_SIZE, SPN_WASM_HEAP_SIZE, error, sizeof(error));
  if (!script->instance) {
    wasm_runtime_unload(script->module);
    sp_mem_arena_destroy(script->arena);
    return fail(unit, SPN_ERR_WASM_MODULE_INSTANCE_FAILED, (spn_err_wasm_t) {
      .path = module,
      .error = sp_cstr_as_str(error),
    });
  }

  script->env = wasm_runtime_create_exec_env(script->instance, SPN_WASM_STACK_SIZE);
  if (!script->env) {
    wasm_runtime_deinstantiate(script->instance);
    wasm_runtime_unload(script->module);
    sp_mem_arena_destroy(script->arena);
    return fail(unit, SPN_ERR_WASM_CTX_FAILED, (spn_err_wasm_t) { .path = module });
  }

  script->handles = sp_alloc_type(mem, spn_wasm_handles_t);
  sp_ht_init(mem, script->handles->map);
  script->handles->ctx = sp_ptr_cast(spn_t*, unit);
  script->ctx = spn_wasm_add_handle(script->handles, unit, SPN_ABI_KIND_CTX);
  wasm_runtime_set_user_data(script->env, script->handles);

  script->wasi = spn_dag_wasi_new(mem, roots, mounts, sp_carr_len(mounts), obs);
  spn_dag_wasi_bind(script->wasi, script->instance);

  return SPN_OK;
}

void spn_wasm_script_close(spn_wasm_script_t* script) {
  wasm_runtime_destroy_exec_env(script->env);
  wasm_runtime_deinstantiate(script->instance);
  wasm_runtime_unload(script->module);
  sp_mem_arena_destroy(script->arena);
}

bool spn_wasm_script_exports(spn_wasm_script_t* script, sp_str_t name) {
  c8 buffer [SPN_WASM_EXPORT_MAX] = sp_zero;
  sp_str_copy_to(name, buffer, SPN_WASM_EXPORT_MAX - 1);
  return wasm_runtime_lookup_function(script->instance, buffer) != SP_NULLPTR;
}

spn_err_t spn_wasm_script_call(spn_wasm_script_t* script, sp_str_t name, const spn_wasm_handle_t* handles, u32 count) {
  sp_assert(count <= SPN_WASM_CALL_MAX_HANDLES);
  spn_pkg_unit_t* unit = script->unit;

  c8 buffer [SPN_WASM_EXPORT_MAX] = sp_zero;
  sp_str_copy_to(name, buffer, SPN_WASM_EXPORT_MAX - 1);
  wasm_function_inst_t fn = wasm_runtime_lookup_function(script->instance, buffer);
  if (!fn) {
    return fail(unit, SPN_ERR_WASM_EXPORT_NOT_FOUND, (spn_err_wasm_t) {
      .path = script->path,
      .error = name,
    });
  }

  wasm_val_t args [1 + SPN_WASM_CALL_MAX_HANDLES] = {
    { .kind = WASM_I32, .of.i32 = (s32)script->ctx },
  };
  u32 tokens [SPN_WASM_CALL_MAX_HANDLES] = sp_zero;
  sp_for(it, count) {
    tokens[it] = spn_wasm_add_handle(script->handles, handles[it].ptr, handles[it].kind);
    args[1 + it] = (wasm_val_t) { .kind = WASM_I32, .of.i32 = (s32)tokens[it] };
  }

  wasm_val_t results [1] = sp_zero;
  spn_wasm_script_t* previous = active;
  active = script;
  bool called = wasm_runtime_call_wasm_a(script->env, fn, 1, results, 1 + count, args);
  active = previous;
  sp_for(it, count) {
    spn_wasm_remove_handle(script->handles, tokens[it]);
  }

  if (!called) {
    return fail(unit, SPN_ERR_WASM_MODULE_CALL_FAILED, (spn_err_wasm_t) {
      .path = script->path,
      .error = sp_cstr_as_str(wasm_runtime_get_exception(script->instance)),
    });
  }
  if (results[0].of.i32) {
    return fail(unit, SPN_ERR_WASM_SCRIPT_ERROR, (spn_err_wasm_t) {
      .path = script->path,
      .rc = results[0].of.i32,
    });
  }
  return SPN_OK;
}

bool spn_wasm_trap(sp_str_t message) {
  if (!active) {
    return false;
  }
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  wasm_runtime_set_exception(active->instance, sp_str_to_cstr(s.mem, message));
  sp_mem_end_scratch(s);
  return true;
}
