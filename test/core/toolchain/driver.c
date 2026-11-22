#include "toolchain.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  spn_triple_t target;
  bool expect;
} composes_t;

static const composes_t composes_tests [] = {
  { "gcc_gnu",    SPN_CC_DRIVER_GCC,   HOST_X64_LINUX, true },
  { "gcc_darwin", SPN_CC_DRIVER_GCC,   HOST_ARM_MACOS, true },
  { "gcc_link",   SPN_CC_DRIVER_GCC,   TARGET_WIN_MSVC },
  { "clang_wasm", SPN_CC_DRIVER_CLANG, TARGET_WASM, true },
  { "zig_link",   SPN_CC_DRIVER_ZIG,   TARGET_WIN_MSVC, true },
  { "msvc_link",  SPN_CC_DRIVER_MSVC,  TARGET_WIN_MSVC, true },
  { "msvc_gnu",   SPN_CC_DRIVER_MSVC,  HOST_X64_LINUX },
};

sp_test_each(driver, composes, composes_t, composes_tests) {
  sp_expect_eq(t, it->expect, spn_toolchain_driver_composes(it->driver, it->target));
  return SP_OK;
}
