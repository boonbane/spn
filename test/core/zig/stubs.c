#include "spn_test.h"

#include "external/zig.h"
#include "toolchain/sdk.h"
#include "triple/triple.h"

#define STUB_TEST_MAX_LIBS 4

typedef struct {
  spn_cc_output_kind_t kind;
  spn_lang_t lang;
  spn_runtime_t libc;
  const c8* libs [STUB_TEST_MAX_LIBS];
} link_t;

typedef struct {
  bool is_static;
  bool sdk;
  const c8* libs [STUB_TEST_MAX_LIBS];
} expect_t;

typedef struct {
  const c8* name;
  spn_triple_t triple;
  spn_sanitizer_set_t sanitizers;
  const c8* sdk;
  link_t link;
  expect_t expect;
} test_t;

#define LINUX_MUSL { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_MUSL }
#define ARM_MACOS  { SPN_ARCH_ARM64, SPN_OS_MACOS, SPN_ABI_NONE }
#define WIN_GNU    { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_GNU }
#define WASI       { SPN_ARCH_WASM32, SPN_OS_WASI, SPN_ABI_NONE }

static const test_t tests [] = {
  {
    .name = "exe_c",
    .triple = LINUX_MUSL,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
  },
  {
    .name = "shared_cxx",
    .triple = LINUX_MUSL,
    .link = { SPN_CC_OUTPUT_SHARED_LIB, SPN_LANG_CXX },
  },
  {
    .name = "reactor",
    .triple = WASI,
    .link = { SPN_CC_OUTPUT_REACTOR, SPN_LANG_C },
  },
  {
    .name = "macos",
    .triple = ARM_MACOS,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
  },
  {
    .name = "sanitizers",
    .triple = LINUX_MUSL,
    .sanitizers = SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
  },
  {
    .name = "static_exe",
    .triple = LINUX_MUSL,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, SPN_RUNTIME_STATIC },
    .expect = { .is_static = true },
  },
  {
    .name = "static_dropped_for_shared",
    .triple = LINUX_MUSL,
    .link = { SPN_CC_OUTPUT_SHARED_LIB, SPN_LANG_C, SPN_RUNTIME_STATIC },
  },
  {
    .name = "sysroot_keyed",
    .triple = LINUX_MUSL,
    .sdk = "/sdk",
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
    .expect = { .sdk = true },
  },
  {
    .name = "windows_libs_sorted",
    .triple = WIN_GNU,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, .libs = { "B", "A" } },
    .expect = { .libs = { "A", "B" } },
  },
  {
    .name = "windows_libs_deduped",
    .triple = WIN_GNU,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, .libs = { "A", "A", "B" } },
    .expect = { .libs = { "A", "B" } },
  },
  {
    .name = "libs_dropped_elsewhere",
    .triple = LINUX_MUSL,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, .libs = { "A", "B" } },
  },
};

sp_test_each(zig_stubs, tuple, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  spn_profile_info_t profile = {
    .arch = it->triple.arch,
    .os = it->triple.os,
    .abi = it->triple.abi,
    .sanitizers = it->sanitizers,
    .linking.libc = it->link.libc,
  };
  if (it->sdk) {
    profile.sdk = spn_sdk_at(mem, it->triple, (spn_path_t) { .sub = sp_cstr_as_str(it->sdk) });
  }
  spn_cc_link_t link = {
    .kind = it->link.kind,
    .lang = it->link.lang,
    .system_libs = sp_da_new(mem, sp_str_t),
  };
  u32 libs = 0;
  sp_carr_detect_len(it->link.libs, libs, it->link.libs[libs]);
  sp_for(b, libs) {
    sp_da_push(link.system_libs, sp_cstr_as_str(it->link.libs[b]));
  }

  spn_zig_stub_t stub = spn_zig_stub(mem, &profile, &link);
  sp_expect(t, spn_triple_equal(stub.triple, it->triple));
  sp_expect_eq(t, stub.kind, it->link.kind);
  sp_expect_eq(t, stub.lang, it->link.lang);
  sp_expect_eq(t, stub.is_static, it->expect.is_static);
  sp_expect_eq(t, stub.sanitizers, it->sanitizers);
  sp_expect_eq(t, stub.sdk != 0, it->expect.sdk);

  u32 expected = 0;
  sp_carr_detect_len(it->expect.libs, expected, it->expect.libs[expected]);
  sp_must_eq(t, sp_da_size(stub.system_libs), expected);
  sp_for(b, expected) {
    sp_test_kv(t, "lib", sp_fmt(mem, "{}", sp_fmt_uint(b)).value);
    sp_expect_str_eq_c(t, stub.system_libs[b], it->expect.libs[b]);
  }
  return SP_OK;
}
