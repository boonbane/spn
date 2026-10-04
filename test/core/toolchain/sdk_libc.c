#include "toolchain.h"

#define SDK_LIBC_STORAGE_DIR "/T"

typedef struct {
  const c8* name;
  fixture_sdk_t sdk;
  const c8* expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "macos_names_the_sdk_headers",
    .sdk = { SPN_SDK_MACOS, { "/S" } },
    .expect =
      "include_dir=/S/usr/include\n"
      "sys_include_dir=/S/usr/include\n"
      "crt_dir=\n"
      "msvc_lib_dir=\n"
      "kernel32_lib_dir=\n"
      "gcc_dir=\n",
  },
  {
    .name = "msvc_names_every_role",
    .sdk = { SPN_SDK_MSVC, { "/X" }, SPN_ARCH_X64 },
    .expect =
      "include_dir=/X/sdk/include/ucrt\n"
      "sys_include_dir=/X/crt/include\n"
      "crt_dir=/X/sdk/lib/ucrt/x86_64\n"
      "msvc_lib_dir=/X/crt/lib/x86_64\n"
      "kernel32_lib_dir=/X/sdk/lib/um/x86_64\n"
      "gcc_dir=\n",
  },
  {
    .name = "rooted_dirs_render_absolute",
    .sdk = { SPN_SDK_MSVC, { "aa/X", SPN_PATH_ROOT_STORAGE }, SPN_ARCH_X64 },
    .expect =
      "include_dir=" SDK_LIBC_STORAGE_DIR "/aa/X/sdk/include/ucrt\n"
      "sys_include_dir=" SDK_LIBC_STORAGE_DIR "/aa/X/crt/include\n"
      "crt_dir=" SDK_LIBC_STORAGE_DIR "/aa/X/sdk/lib/ucrt/x86_64\n"
      "msvc_lib_dir=" SDK_LIBC_STORAGE_DIR "/aa/X/crt/lib/x86_64\n"
      "kernel32_lib_dir=" SDK_LIBC_STORAGE_DIR "/aa/X/sdk/lib/um/x86_64\n"
      "gcc_dir=\n",
  },
};

sp_test_each(sdk_libc, render, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  roots.dirs[SPN_PATH_ROOT_STORAGE] = sp_str_lit(SDK_LIBC_STORAGE_DIR);
  spn_sdk_t sdk = fixture_sdk(mem, it->sdk);

  sp_io_dyn_mem_writer_t w = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &w);
  spn_libc_render(&w.base, &roots, &sdk);
  sp_expect_str_eq_c(t, sp_io_dyn_mem_writer_as_str(&w), it->expect);
  return SP_OK;
}
