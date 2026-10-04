#include "toolchain/libc.h"

#include "paths/paths.h"
#include "str/str.h"

typedef struct {
  spn_path_t include;
  spn_path_t sys_include;
  spn_path_t crt;
  spn_path_t msvc_lib;
  spn_path_t kernel32_lib;
} libc_t;

static void render_key(sp_io_writer_t* io, const spn_path_roots_t* roots, const c8* key, spn_path_t path) {
  sp_str_buf_t buf = sp_zero;
  sp_fmt_io(io, "{}={}\n", sp_fmt_cstr(key), sp_fmt_str(spn_path_str(roots, sp_str_buf_as_mem(&buf), path)));
}

void spn_libc_render(sp_io_writer_t* io, const spn_path_roots_t* roots, const spn_sdk_t* sdk) {
  libc_t libc = sp_zero;
  switch (sdk->kind) {
    case SPN_SDK_MACOS: {
      libc = (libc_t) {
        .include = sdk->macos.include,
        .sys_include = sdk->macos.include,
      };
      break;
    }
    case SPN_SDK_MSVC: {
      libc = (libc_t) {
        .include = sdk->msvc.include.ucrt,
        .sys_include = sdk->msvc.include.vc,
        .crt = sdk->msvc.lib.ucrt,
        .msvc_lib = sdk->msvc.lib.vc,
        .kernel32_lib = sdk->msvc.lib.um,
      };
      break;
    }
    case SPN_SDK_NONE:
    case SPN_SDK_SYSROOT: {
      sp_unreachable_case();
    }
  }

  render_key(io, roots, "include_dir", libc.include);
  render_key(io, roots, "sys_include_dir", libc.sys_include);
  render_key(io, roots, "crt_dir", libc.crt);
  render_key(io, roots, "msvc_lib_dir", libc.msvc_lib);
  render_key(io, roots, "kernel32_lib_dir", libc.kernel32_lib);
  render_key(io, roots, "gcc_dir", (spn_path_t) sp_zero);
}
