#include "toolchain.h"

typedef struct {
  test_path_t include [SPN_DIR_LIST_MAX];
  test_path_t lib [SPN_DIR_LIST_MAX];
} msvc_expect_t;

typedef struct {
  test_path_t include;
  test_path_t lib;
  test_path_t frameworks;
} macos_expect_t;

typedef struct {
  test_path_t root;
  macos_expect_t macos;
  msvc_expect_t msvc;
} expect_t;

typedef struct {
  const c8* name;
  fixture_sdk_t sdk;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "sysroot_keeps_root",
    .sdk = { SPN_SDK_SYSROOT, { "/S" } },
    .expect = { .root = { "/S" } },
  },
  {
    .name = "macos_lays_out_headers_libs_and_frameworks",
    .sdk = { SPN_SDK_MACOS, { "/S" } },
    .expect = { .root = { "/S" }, .macos = { { "/S/usr/include" }, { "/S/usr/lib" }, { "/S/System/Library/Frameworks" } } },
  },
  {
    .name = "rooted_root_is_kept",
    .sdk = { SPN_SDK_SYSROOT, { "aa/S", SPN_PATH_ROOT_TOOLCHAIN } },
    .expect = { .root = { "aa/S", SPN_PATH_ROOT_TOOLCHAIN } },
  },
  {
    .name = "msvc_lays_out_xwin_x64",
    .sdk = { SPN_SDK_MSVC, { "/X" }, SPN_ARCH_X64 },
    .expect = {
      .msvc = {
        .include = {
          { "/X/crt/include" },
          { "/X/sdk/include/ucrt" },
          { "/X/sdk/include/um" },
          { "/X/sdk/include/shared" },
          { "/X/sdk/include/winrt" },
          { "/X/sdk/include/cppwinrt" },
        },
        .lib = {
          { "/X/crt/lib/x86_64" },
          { "/X/sdk/lib/ucrt/x86_64" },
          { "/X/sdk/lib/um/x86_64" },
        },
      },
    },
  },
  {
    .name = "msvc_lays_out_xwin_arm64",
    .sdk = { SPN_SDK_MSVC, { "/X" }, SPN_ARCH_ARM64 },
    .expect = {
      .msvc = {
        .include = {
          { "/X/crt/include" },
          { "/X/sdk/include/ucrt" },
          { "/X/sdk/include/um" },
          { "/X/sdk/include/shared" },
          { "/X/sdk/include/winrt" },
          { "/X/sdk/include/cppwinrt" },
        },
        .lib = {
          { "/X/crt/lib/aarch64" },
          { "/X/sdk/lib/ucrt/aarch64" },
          { "/X/sdk/lib/um/aarch64" },
        },
      },
    },
  },
  {
    .name = "msvc_dirs_keep_the_root",
    .sdk = { SPN_SDK_MSVC, { "aa/X", SPN_PATH_ROOT_TOOLCHAIN }, SPN_ARCH_X64 },
    .expect = {
      .msvc = {
        .include = {
          { "aa/X/crt/include", SPN_PATH_ROOT_TOOLCHAIN },
          { "aa/X/sdk/include/ucrt", SPN_PATH_ROOT_TOOLCHAIN },
          { "aa/X/sdk/include/um", SPN_PATH_ROOT_TOOLCHAIN },
          { "aa/X/sdk/include/shared", SPN_PATH_ROOT_TOOLCHAIN },
          { "aa/X/sdk/include/winrt", SPN_PATH_ROOT_TOOLCHAIN },
          { "aa/X/sdk/include/cppwinrt", SPN_PATH_ROOT_TOOLCHAIN },
        },
        .lib = {
          { "aa/X/crt/lib/x86_64", SPN_PATH_ROOT_TOOLCHAIN },
          { "aa/X/sdk/lib/ucrt/x86_64", SPN_PATH_ROOT_TOOLCHAIN },
          { "aa/X/sdk/lib/um/x86_64", SPN_PATH_ROOT_TOOLCHAIN },
        },
      },
    },
  },
};

static sp_err_t check_dirs(sp_test_t* t, spn_dir_list_t dirs, const test_path_t* expect, u32 count) {
  sp_must_eq(t, count, dirs.count);
  sp_for(it, count) {
    if (test_check_path(t, dirs.items[it], expect[it])) {
      return SP_ERR;
    }
  }
  return SP_OK;
}

sp_test_each(sdk_root, layout, test_t, tests) {
  spn_sdk_t sdk = fixture_sdk(sp_test_arena(t), it->sdk);
  sp_must_eq(t, (u32)it->sdk.kind, (u32)sdk.kind);
  switch (sdk.kind) {
    case SPN_SDK_SYSROOT: {
      return test_check_path(t, sdk.root, it->expect.root);
    }
    case SPN_SDK_MACOS: {
      if (test_check_path(t, sdk.macos.root, it->expect.root) || test_check_path(t, sdk.macos.include, it->expect.macos.include) || test_check_path(t, sdk.macos.lib, it->expect.macos.lib)) {
        return SP_ERR;
      }
      return test_check_path(t, sdk.macos.frameworks, it->expect.macos.frameworks);
    }
    case SPN_SDK_MSVC: {
      sp_expect_eq(t, (u32)it->sdk.arch, (u32)sdk.msvc.arch);
      u32 include = 0;
      sp_carr_detect_len(it->expect.msvc.include, include, it->expect.msvc.include[include].path);
      u32 lib = 0;
      sp_carr_detect_len(it->expect.msvc.lib, lib, it->expect.msvc.lib[lib].path);
      if (check_dirs(t, spn_msvc_include_dirs(&sdk.msvc), it->expect.msvc.include, include)) {
        return SP_ERR;
      }
      return check_dirs(t, spn_msvc_lib_dirs(&sdk.msvc), it->expect.msvc.lib, lib);
    }
    case SPN_SDK_NONE: {
      sp_unreachable_case();
    }
  }
  sp_unreachable_return(SP_ERR);
}
