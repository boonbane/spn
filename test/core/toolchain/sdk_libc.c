#include "toolchain.h"

#define SDK_LIBC_STORAGE_DIR "/T"

typedef struct {
  spn_sdk_kind_t kind;
  struct {
    test_path_t include;
    test_path_t frameworks;
  } macos;
  struct {
    struct {
      test_path_t ucrt;
      test_path_t vc;
    } include;
    struct {
      test_path_t ucrt;
      test_path_t vc;
      test_path_t um;
    } lib;
  } msvc;
} sdk_t;

typedef struct {
  const c8* name;
  sdk_t sdk;
  const c8* expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "macos_names_the_sdk_headers",
    .sdk = { SPN_SDK_MACOS, .macos = { .include = { "/A" } } },
    .expect =
      "include_dir=/A\n"
      "sys_include_dir=/A\n"
      "crt_dir=\n"
      "msvc_lib_dir=\n"
      "kernel32_lib_dir=\n"
      "gcc_dir=\n",
  },
  {
    .name = "msvc_names_every_role",
    .sdk = { SPN_SDK_MSVC, .msvc = { .include = { .ucrt = { "/A" }, .vc = { "/B" } }, .lib = { .ucrt = { "/C" }, .vc = { "/D" }, .um = { "/E" } } } },
    .expect =
      "include_dir=/A\n"
      "sys_include_dir=/B\n"
      "crt_dir=/C\n"
      "msvc_lib_dir=/D\n"
      "kernel32_lib_dir=/E\n"
      "gcc_dir=\n",
  },
  {
    .name = "rooted_dirs_render_absolute",
    .sdk = { SPN_SDK_MACOS, .macos = { .include = { "A", SPN_PATH_ROOT_STORAGE } } },
    .expect =
      "include_dir=" SDK_LIBC_STORAGE_DIR "/A\n"
      "sys_include_dir=" SDK_LIBC_STORAGE_DIR "/A\n"
      "crt_dir=\n"
      "msvc_lib_dir=\n"
      "kernel32_lib_dir=\n"
      "gcc_dir=\n",
  },
};

static spn_sdk_t make_sdk(sdk_t sdk) {
  switch (sdk.kind) {
    case SPN_SDK_NONE:
    case SPN_SDK_SYSROOT:
    case SPN_SDK_HOST: {
      return (spn_sdk_t) { .kind = sdk.kind };
    }
    case SPN_SDK_MACOS: {
      return (spn_sdk_t) {
        .kind = SPN_SDK_MACOS,
        .macos = {
          .include = fixture_path(sdk.macos.include),
          .frameworks = fixture_path(sdk.macos.frameworks),
        },
      };
    }
    case SPN_SDK_MSVC: {
      return (spn_sdk_t) {
        .kind = SPN_SDK_MSVC,
        .msvc = {
          .include = {
            .ucrt = fixture_path(sdk.msvc.include.ucrt),
            .vc = fixture_path(sdk.msvc.include.vc),
          },
          .lib = {
            .ucrt = fixture_path(sdk.msvc.lib.ucrt),
            .vc = fixture_path(sdk.msvc.lib.vc),
            .um = fixture_path(sdk.msvc.lib.um),
          },
        },
      };
    }
  }
  sp_unreachable_return(sp_zero_s(spn_sdk_t));
}

sp_test_each(sdk_libc, render, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  roots.dirs[SPN_PATH_ROOT_STORAGE] = sp_str_lit(SDK_LIBC_STORAGE_DIR);
  spn_sdk_t sdk = make_sdk(it->sdk);
  spn_libc_t libc = sp_zero;
  sp_must(t, spn_sdk_libc(SPN_CC_CAP_LIBC_FILE, &sdk, &libc));

  sp_io_dyn_mem_writer_t w = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &w);
  spn_libc_render(&w.base, &roots, &libc);
  sp_expect_str_eq_c(t, sp_io_dyn_mem_writer_as_str(&w), it->expect);
  return SP_OK;
}

typedef struct {
  bool libc;
  test_path_t frameworks;
} driver_expect_t;

typedef struct {
  const c8* name;
  spn_cc_cap_set_t caps;
  sdk_t sdk;
  driver_expect_t expect;
} driver_test_t;

static const driver_test_t driver_tests [] = {
  { .name = "none_ignores_libc_file",    .sdk = { SPN_SDK_NONE },                                        .caps = SPN_CC_CAP_LIBC_FILE },
  { .name = "sysroot_ignores_libc_file", .sdk = { SPN_SDK_SYSROOT },                                     .caps = SPN_CC_CAP_LIBC_FILE },
  { .name = "macos",                     .sdk = { SPN_SDK_MACOS, .macos = { .frameworks = { "/A" } } } },
  { .name = "macos_with_libc_file",      .sdk = { SPN_SDK_MACOS, .macos = { .frameworks = { "/A" } } }, .caps = SPN_CC_CAP_LIBC_FILE, .expect = { .libc = true, .frameworks = { "/A" } } },
};

sp_test_each(sdk_libc, driver, driver_test_t, driver_tests) {
  spn_sdk_t sdk = make_sdk(it->sdk);
  spn_libc_t libc = sp_zero;
  sp_expect_eq(t, (u32)it->expect.libc, (u32)spn_sdk_libc(it->caps, &sdk, &libc));
  return test_check_path(t, libc.frameworks, it->expect.frameworks);
}
