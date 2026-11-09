#include "sp.h"
#include "dag/dag.h"
#include "external/wasm/wasi.h"
#include "fs/fs.h"
#include "paths/paths.h"
#include "external/wasm/abi.h"
#include "unit/types.h"
#include "api/api.h"
#include "ctx/types.h"

#define SPN_GUEST_FMT_ARGS 4

void spn_abi_io_write(spn_wasm_ctx_t* abi, const c8* path, const c8* contents) {
  spn_pkg_unit_t* unit = spn_api_unit(abi->handles->ctx);
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t guest = sp_cstr_as_str(path);
  if (!spn_path_normal(guest)) {
    wasm_runtime_set_exception(abi->instance, sp_fmt_mem_cstr(scratch.mem, "{} must not contain '.', '..', or empty components", sp_fmt_cstr(path)));
    sp_mem_end_scratch(scratch);
    return;
  }
  spn_path_t dst = sp_zero;
  if (!spn_wasi_resolve(abi->instance, scratch.mem, guest, &dst)) {
    wasm_runtime_set_exception(abi->instance, sp_fmt_mem_cstr(scratch.mem, "{} is not under /work, /source, /manifest, or /store", sp_fmt_cstr(path)));
    sp_mem_end_scratch(scratch);
    return;
  }
  sp_str_t dst_str = spn_path_str(&unit->units->ctx->roots, scratch.mem, dst);

  sp_fs_create_parent_at(spn_path_at(&unit->units->ctx->roots, dst));

  sp_io_file_writer_t writer = sp_zero;
  if (sp_io_file_writer_from_path_at(&writer, spn_path_at(&unit->units->ctx->roots, dst))) {
    wasm_runtime_set_exception(abi->instance, sp_fmt_mem_cstr(scratch.mem, "spn_io_write: {}", SP_FMT_STR(dst_str)));
    sp_mem_end_scratch(scratch);
    return;
  }
  spn_wasi_observe_write(abi->instance, dst);

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
