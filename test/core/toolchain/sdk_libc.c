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
  spn_sdk_t sdk = spn_sdk_for_driver(SPN_CC_CAP_LIBC_FILE, fixture_sdk(mem, it->sdk));
  sp_must_eq(t, (u32)SPN_SDK_LIBC, (u32)sdk.kind);

  sp_io_dyn_mem_writer_t w = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &w);
  spn_libc_render(&w.base, &roots, &sdk.libc);
  sp_expect_str_eq_c(t, sp_io_dyn_mem_writer_as_str(&w), it->expect);
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_cc_cap_set_t caps;
  fixture_sdk_t sdk;
  spn_sdk_kind_t expect;
  test_path_t root;
  test_path_t frameworks;
} driver_test_t;

static const driver_test_t driver_tests [] = {
  { .name = "none",                      .sdk = { SPN_SDK_NONE },                         .expect = SPN_SDK_NONE },
  { .name = "none_ignores_libc_file",    .sdk = { SPN_SDK_NONE },                         .expect = SPN_SDK_NONE,    .caps = SPN_CC_CAP_LIBC_FILE },
  { .name = "sysroot",                   .sdk = { SPN_SDK_SYSROOT, { "/S" } },            .expect = SPN_SDK_SYSROOT, .root = { "/S" } },
  { .name = "sysroot_ignores_libc_file", .sdk = { SPN_SDK_SYSROOT, { "/S" } },            .expect = SPN_SDK_SYSROOT, .root = { "/S" }, .caps = SPN_CC_CAP_LIBC_FILE },
  { .name = "macos",                     .sdk = { SPN_SDK_MACOS, { "/S" } },              .expect = SPN_SDK_MACOS,   .root = { "/S" } },
  { .name = "macos_with_libc_file",      .sdk = { SPN_SDK_MACOS, { "/S" } },              .expect = SPN_SDK_LIBC,    .caps = SPN_CC_CAP_LIBC_FILE, .frameworks = { "/S/System/Library/Frameworks" } },
  { .name = "msvc",                      .sdk = { SPN_SDK_MSVC, { "/X" }, SPN_ARCH_X64 }, .expect = SPN_SDK_MSVC },
  { .name = "msvc_with_libc_file",       .sdk = { SPN_SDK_MSVC, { "/X" }, SPN_ARCH_X64 }, .expect = SPN_SDK_LIBC,    .caps = SPN_CC_CAP_LIBC_FILE },
};

sp_test_each(sdk_for_driver, kind, driver_test_t, driver_tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_sdk_t sdk = spn_sdk_for_driver(it->caps, fixture_sdk(mem, it->sdk));
  sp_must_eq(t, (u32)it->expect, (u32)sdk.kind);
  switch (sdk.kind) {
    case SPN_SDK_NONE: {
      return SP_OK;
    }
    case SPN_SDK_SYSROOT: {
      return test_check_path(t, sdk.root, it->root);
    }
    case SPN_SDK_MACOS: {
      return test_check_path(t, sdk.macos.root, it->root);
    }
    case SPN_SDK_MSVC: {
      return test_check_path(t, sdk.msvc.lib.vc, (test_path_t) { "/X/crt/lib/x86_64" });
    }
    case SPN_SDK_LIBC: {
      sp_expect(t, spn_path_empty(sdk.libc.file));
      if (it->frameworks.path) {
        return test_check_path(t, sdk.libc.frameworks, it->frameworks);
      }
      sp_expect(t, spn_path_empty(sdk.libc.frameworks));
      return SP_OK;
    }
  }
  sp_unreachable_return(SP_ERR);
}
