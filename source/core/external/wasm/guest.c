#include "sp.h"
#include "dag/dag.h"
#include "dag/wasi.h"
#include "fs/fs.h"
#include "paths/paths.h"
#include "external/wasm/abi.h"
#include "unit/types.h"
#include "api/api.h"
#include "ctx/types.h"
#include "event/types.h"
#include "event/event.h"
#include "glob/glob.h"

#define SPN_GUEST_FMT_ARGS 4

static spn_pkg_unit_t* guest_unit(spn_wasm_ctx_t* abi) {
  return spn_api_unit(abi->handles->ctx);
}

static bool guest_path(spn_wasm_ctx_t* abi, sp_mem_t mem, const c8* path, spn_path_t* host) {
  sp_str_t str = sp_cstr_as_str(path);
  if (!spn_path_normal(str)) {
    wasm_runtime_set_exception(abi->instance, sp_fmt_mem_cstr(mem, "{} must not contain '.', '..', or empty components", sp_fmt_cstr(path)));
    return false;
  }
  if (!spn_dag_wasi_resolve(abi->instance, mem, str, host)) {
    wasm_runtime_set_exception(abi->instance, sp_fmt_mem_cstr(mem, "{} is not under /work, /source, /manifest, or /store", sp_fmt_cstr(path)));
    return false;
  }
  return true;
}

static void guest_copy(spn_wasm_ctx_t* abi, const c8* name, const c8* from, const c8* to) {
  spn_pkg_unit_t* unit = guest_unit(abi);
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_path_t from_path = sp_zero;
  spn_path_t to_path = sp_zero;
  if (!guest_path(abi, scratch.mem, from, &from_path) || !guest_path(abi, scratch.mem, to, &to_path)) {
    sp_mem_end_scratch(scratch);
    return;
  }

  sp_str_t from_str = spn_path_str(&spn.roots, scratch.mem, from_path);
  sp_str_t to_str = spn_path_str(&spn.roots, scratch.mem, to_path);
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_API_CALL,
    .pkg = unit->info->name,
    .api_call = { .fn = sp_cstr_as_str(name), .args = sp_fmt(spn.mem, "{} -> {}", SP_FMT_STR(from_str), SP_FMT_STR(to_str)).value },
  });

  if (!sp_glob_parse_meta(from_path.sub).literal) {
    spn_dag_wasi_observe_glob(abi->instance, spn_path_parent(from_path), sp_fs_get_name(from_path.sub));
  }
  else {
    spn_dag_wasi_observe_read(abi->instance, from_path);
  }

  if (spn_api_copy(from_path, to_path)) {
    wasm_runtime_set_exception(abi->instance, sp_fmt_mem_cstr(scratch.mem, "{}: {} -> {}", SP_FMT_CSTR(name), SP_FMT_STR(from_str), SP_FMT_STR(to_str)));
  }
  else {
    spn_dag_wasi_observe_write(abi->instance, to_path);
  }
  sp_mem_end_scratch(scratch);
}

void spn_abi_fs_copy(spn_wasm_ctx_t* abi, const c8* from, const c8* to) {
  guest_copy(abi, "spn_fs_copy", from, to);
}

void spn_abi_fs_copy_glob(spn_wasm_ctx_t* abi, const c8* glob, const c8* dir) {
  guest_copy(abi, "spn_fs_copy_glob", glob, dir);
}

void spn_abi_fs_create_dir(spn_wasm_ctx_t* abi, const c8* path) {
  spn_pkg_unit_t* unit = guest_unit(abi);
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_path_t dir = sp_zero;
  if (!guest_path(abi, scratch.mem, path, &dir)) {
    sp_mem_end_scratch(scratch);
    return;
  }
  sp_str_t dir_str = spn_path_str(&spn.roots, scratch.mem, dir);
  SPN_API_LOG(unit, "spn_fs_create_dir", "{}", SP_FMT_STR(dir_str));

  if (sp_fs_create_dir_at(spn_path_at(&spn.roots, dir))) {
    wasm_runtime_set_exception(abi->instance, sp_fmt_mem_cstr(scratch.mem, "spn_fs_create_dir: {}", SP_FMT_STR(dir_str)));
  }
  else {
    spn_dag_wasi_observe_write(abi->instance, dir);
  }
  sp_mem_end_scratch(scratch);
}

void spn_abi_io_write(spn_wasm_ctx_t* abi, const c8* path, const c8* contents) {
  spn_pkg_unit_t* unit = guest_unit(abi);
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_path_t dst = sp_zero;
  if (!guest_path(abi, scratch.mem, path, &dst)) {
    sp_mem_end_scratch(scratch);
    return;
  }
  sp_str_t dst_str = spn_path_str(&spn.roots, scratch.mem, dst);
  SPN_API_LOG(unit, "spn_io_write", "{}", SP_FMT_STR(dst_str));

  sp_fs_create_parent_at(spn_path_at(&spn.roots, dst));

  sp_io_file_writer_t writer = sp_zero;
  if (sp_io_file_writer_from_path_at(&writer, spn_path_at(&spn.roots, dst))) {
    wasm_runtime_set_exception(abi->instance, sp_fmt_mem_cstr(scratch.mem, "spn_io_write: {}", SP_FMT_STR(dst_str)));
    sp_mem_end_scratch(scratch);
    return;
  }
  spn_dag_wasi_observe_write(abi->instance, dst);

  sp_io_write_cstr(&writer.base, contents, SP_NULLPTR);
  sp_io_file_writer_close(&writer);
  sp_mem_end_scratch(scratch);
}

const c8* spn_abi_fmt(spn_wasm_ctx_t* abi, const c8* fmt, const c8* a0, const c8* a1, const c8* a2, const c8* a3) {
  const c8* args [SPN_GUEST_FMT_ARGS] = { a0, a1, a2, a3 };

  sp_io_dyn_mem_writer_t io = sp_zero;
  sp_io_dyn_mem_writer_init(spn.mem, &io);

  u32 next = 0;
  for (const c8* it = fmt; *it;) {
    if (it[0] == '{' && it[1] == '{') {
      sp_io_write_c8(&io.base, '{');
      it += 2;
      continue;
    }
    if (it[0] == '}' && it[1] == '}') {
      sp_io_write_c8(&io.base, '}');
      it += 2;
      continue;
    }
    if (it[0] == '{' && it[1] == '}') {
      if (next >= SPN_GUEST_FMT_ARGS || !args[next]) {
        wasm_runtime_set_exception(abi->instance, "spn_fmt: more placeholders than arguments");
        return SP_NULLPTR;
      }
      sp_io_write_cstr(&io.base, args[next++], SP_NULLPTR);
      it += 2;
      continue;
    }
    if (it[0] == '{' || it[0] == '}') {
      wasm_runtime_set_exception(abi->instance, "spn_fmt: bad placeholder; only {} is supported");
      return SP_NULLPTR;
    }
    sp_io_write_c8(&io.base, *it);
    it++;
  }

  sp_io_write_c8(&io.base, '\0');
  return sp_io_dyn_mem_writer_as_cstr(&io);
}
