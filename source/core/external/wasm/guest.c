#include "sp.h"
#include "external/wasm/abi.h"
#include "ctx/types.h"

#define SPN_GUEST_FMT_ARGS 4

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
