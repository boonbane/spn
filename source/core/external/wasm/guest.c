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

static spn_err_t write_file(spn_wasm_ctx_t* abi, sp_mem_t mem, spn_root_t root, sp_str_t sub, const c8* contents) {
  spn_pkg_unit_t* unit = spn_api_unit(abi->handles->ctx);
  spn_path_t dst = sp_zero;
  spn_try(spn_wasi_resolve(abi->instance, mem, root, sub, &dst));

  sp_path_t at = spn_path_at(&unit->units->ctx->roots, dst);
  sp_fs_create_parent_at(at);

  sp_io_file_writer_t writer = sp_zero;
  if (sp_io_file_writer_from_path_at(&writer, at)) {
    return SPN_ERR_IO_WRITE;
  }
  spn_wasi_observe_write(abi->instance, dst);

  sp_err_t written = sp_io_write_cstr(&writer.base, contents, SP_NULLPTR);
  sp_err_t closed = sp_io_file_writer_close(&writer);
  return written || closed ? SPN_ERR_IO_WRITE : SPN_OK;
}

spn_err_t spn_abi_io_write(spn_wasm_ctx_t* abi, spn_root_t root, const c8* sub, const c8* contents) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = write_file(abi, scratch.mem, root, sp_cstr_as_str(sub), contents);
  sp_mem_end_scratch(scratch);
  return err;
}

const c8* spn_fmt_ex(const c8* fmt, const c8* a0, const c8* a1, const c8* a2, const c8* a3) {
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
        return SP_NULLPTR;
      }
      sp_io_write_cstr(&io.base, args[next++], SP_NULLPTR);
      it += 2;
      continue;
    }
    if (it[0] == '{' || it[0] == '}') {
      return SP_NULLPTR;
    }
    sp_io_write_c8(&io.base, *it);
    it++;
  }

  sp_io_write_c8(&io.base, '\0');
  return sp_io_dyn_mem_writer_as_cstr(&io);
}
