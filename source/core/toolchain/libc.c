#include "toolchain/libc.h"

#include "paths/paths.h"
#include "str/str.h"

static void render_key(sp_io_writer_t* io, const spn_path_roots_t* roots, const c8* key, spn_path_t path) {
  sp_str_buf_t buf = sp_zero;
  sp_fmt_io(io, "{}={}\n", sp_fmt_cstr(key), sp_fmt_str(spn_path_str(roots, sp_str_buf_as_mem(&buf), path)));
}

void spn_libc_render(sp_io_writer_t* io, const spn_path_roots_t* roots, const spn_libc_t* libc) {
  render_key(io, roots, "include_dir", libc->include);
  render_key(io, roots, "sys_include_dir", libc->sys_include);
  render_key(io, roots, "crt_dir", libc->crt);
  render_key(io, roots, "msvc_lib_dir", libc->msvc_lib);
  render_key(io, roots, "kernel32_lib_dir", libc->kernel32_lib);
  render_key(io, roots, "gcc_dir", (spn_path_t) sp_zero);
}
