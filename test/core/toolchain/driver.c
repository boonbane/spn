#include "toolchain.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  spn_cc_cap_set_t expect;
} caps_t;

static const caps_t caps_tests [] = {
  { "gcc",   SPN_CC_DRIVER_GCC,   SPN_CC_CAP_FUSE_LD | SPN_CC_CAP_BARE | SPN_CC_CAP_GNU_RUNTIME },
  { "clang", SPN_CC_DRIVER_CLANG, SPN_CC_CAP_TARGET_TRIPLE | SPN_CC_CAP_LLVM_TRIPLE | SPN_CC_CAP_CLANG_FRONTEND | SPN_CC_CAP_FUSE_LD | SPN_CC_CAP_GNU_RUNTIME },
  { "zig",   SPN_CC_DRIVER_ZIG,   SPN_CC_CAP_TARGET_TRIPLE | SPN_CC_CAP_CLANG_FRONTEND | SPN_CC_CAP_CODEVIEW | SPN_CC_CAP_LIBC_FILE | SPN_CC_CAP_DEFAULT_UBSAN | SPN_CC_CAP_BARE },
  { "msvc",  SPN_CC_DRIVER_MSVC,  0 },
};

sp_test_each(driver, caps, caps_t, caps_tests) {
  sp_expect_eq(t, it->expect, spn_toolchain_driver_caps(it->driver));
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  spn_triple_t target;
  bool expect;
} composes_t;

static const composes_t composes_tests [] = {
  { "gcc_gnu",      SPN_CC_DRIVER_GCC,   HOST_X64_LINUX, true },
  { "gcc_link",     SPN_CC_DRIVER_GCC,   TARGET_WIN_MSVC, false },
  { "gcc_darwin",   SPN_CC_DRIVER_GCC,   HOST_ARM_MACOS, true },
  { "gcc_wasm",     SPN_CC_DRIVER_GCC,   TARGET_WASM, false },
  { "clang_gnu",    SPN_CC_DRIVER_CLANG, HOST_X64_LINUX, true },
  { "clang_link",   SPN_CC_DRIVER_CLANG, TARGET_WIN_MSVC, true },
  { "clang_darwin", SPN_CC_DRIVER_CLANG, HOST_ARM_MACOS, true },
  { "clang_wasm",   SPN_CC_DRIVER_CLANG, TARGET_WASM, true },
  { "zig_gnu",      SPN_CC_DRIVER_ZIG,   HOST_X64_LINUX, true },
  { "zig_link",     SPN_CC_DRIVER_ZIG,   TARGET_WIN_MSVC, true },
  { "zig_darwin",   SPN_CC_DRIVER_ZIG,   HOST_ARM_MACOS, true },
  { "zig_wasm",     SPN_CC_DRIVER_ZIG,   TARGET_WASM, true },
  { "msvc_gnu",     SPN_CC_DRIVER_MSVC,  HOST_X64_LINUX, false },
  { "msvc_link",    SPN_CC_DRIVER_MSVC,  TARGET_WIN_MSVC, true },
  { "msvc_darwin",  SPN_CC_DRIVER_MSVC,  HOST_ARM_MACOS, false },
  { "msvc_wasm",    SPN_CC_DRIVER_MSVC,  TARGET_WASM, false },
};

sp_test_each(driver, composes, composes_t, composes_tests) {
  sp_expect_eq(t, it->expect, spn_toolchain_driver_composes(it->driver, it->target));
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  spn_os_t os;
  spn_abi_t expect;
} default_abi_t;

static const default_abi_t default_abi_tests [] = {
  { "gcc_windows",   SPN_CC_DRIVER_GCC,   SPN_OS_WINDOWS,      SPN_ABI_GNU },
  { "clang_windows", SPN_CC_DRIVER_CLANG, SPN_OS_WINDOWS },
  { "zig_windows",   SPN_CC_DRIVER_ZIG,   SPN_OS_WINDOWS,      SPN_ABI_GNU },
  { "msvc_windows",  SPN_CC_DRIVER_MSVC,  SPN_OS_WINDOWS,      SPN_ABI_MSVC },
  { "linux",         SPN_CC_DRIVER_GCC,   SPN_OS_LINUX },
  { "macos",         SPN_CC_DRIVER_CLANG, SPN_OS_MACOS,        SPN_ABI_APPLE },
  { "wasi",          SPN_CC_DRIVER_CLANG, SPN_OS_WASI,         SPN_ABI_MUSL },
};

sp_test_each(driver, default_abi, default_abi_t, default_abi_tests) {
  sp_expect_eq(t, (u32)it->expect, (u32)spn_default_abi(it->driver, it->os));
  return SP_OK;
}

sp_test(driver, retargeting_drivers_compose_every_dialect) {
  sp_carr_for(composes_tests, it) {
    if (spn_toolchain_driver_retargets(composes_tests[it].driver)) {
      sp_expect(t, composes_tests[it].expect);
    }
  }
  return SP_OK;
}
