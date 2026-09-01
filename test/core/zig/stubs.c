#include "spn_test.h"

#include "external/zig.h"
#include "toolchain/sdk.h"

#define STUB_TEST_MAX_LIBS 4

typedef struct {
  spn_cc_output_kind_t kind;
  spn_lang_t lang;
  spn_runtime_t libc;
  const c8* libs [STUB_TEST_MAX_LIBS];
} link_t;

typedef struct {
  spn_linkage_t linkage;
  const c8* libs [STUB_TEST_MAX_LIBS];
  const c8* name;
} expect_t;

typedef struct {
  const c8* name;
  spn_os_t os;
  spn_sanitizer_set_t sanitizers;
  const c8* sdk;
  link_t link;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "exe_c",
    .os = SPN_OS_LINUX,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
    .expect = { .name = "T.exe.c" },
  },
  {
    .name = "shared_cxx",
    .os = SPN_OS_LINUX,
    .link = { SPN_CC_OUTPUT_SHARED_LIB, SPN_LANG_CXX },
    .expect = { .name = "T.shared.cxx" },
  },
  {
    .name = "reactor",
    .os = SPN_OS_WASI,
    .link = { SPN_CC_OUTPUT_REACTOR, SPN_LANG_C },
    .expect = { .name = "T.reactor.c" },
  },
  {
    .name = "sanitizers",
    .os = SPN_OS_LINUX,
    .sanitizers = SPN_SANITIZER_THREAD | SPN_SANITIZER_UNDEFINED,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
    .expect = { .name = "T.exe.c.thread,undefined" },
  },
  {
    .name = "static_exe",
    .os = SPN_OS_LINUX,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, SPN_RUNTIME_STATIC },
    .expect = { .linkage = SPN_LIB_KIND_STATIC, .name = "T.exe.c.static" },
  },
  {
    .name = "static_dropped_for_shared",
    .os = SPN_OS_LINUX,
    .link = { SPN_CC_OUTPUT_SHARED_LIB, SPN_LANG_C, SPN_RUNTIME_STATIC },
    .expect = { .name = "T.shared.c" },
  },
  {
    .name = "static_dropped_on_macos",
    .os = SPN_OS_MACOS,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, SPN_RUNTIME_STATIC },
    .expect = { .name = "T.exe.c" },
  },
  {
    .name = "static_dropped_on_windows",
    .os = SPN_OS_WINDOWS,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, SPN_RUNTIME_STATIC },
    .expect = { .name = "T.exe.c" },
  },
  {
    .name = "sysroot_keyed",
    .os = SPN_OS_LINUX,
    .sdk = "/sdk",
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C },
    .expect = { .name = "T.exe.c.e8f7d229bd748280" },
  },
  {
    .name = "windows_libs_sorted",
    .os = SPN_OS_WINDOWS,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, .libs = { "B", "A" } },
    .expect = { .libs = { "A", "B" }, .name = "T.exe.c.3ebc00ede8f15f3f" },
  },
  {
    .name = "windows_libs_deduped",
    .os = SPN_OS_WINDOWS,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, .libs = { "A", "A", "B" } },
    .expect = { .libs = { "A", "B" }, .name = "T.exe.c.3ebc00ede8f15f3f" },
  },
  {
    .name = "windows_lib_subset_distinct",
    .os = SPN_OS_WINDOWS,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, .libs = { "A" } },
    .expect = { .libs = { "A" }, .name = "T.exe.c.4dc8c028fa4b6832" },
  },
  {
    .name = "libs_dropped_elsewhere",
    .os = SPN_OS_LINUX,
    .link = { SPN_CC_OUTPUT_EXE, SPN_LANG_C, .libs = { "A", "B" } },
    .expect = { .name = "T.exe.c" },
  },
};

sp_test_each(zig_stubs, canonical, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  spn_profile_info_t profile = {
    .os = it->os,
    .linking.libc = it->link.libc,
  };
  if (it->sdk) {
    profile.sdk = spn_sdk_at(mem, (spn_triple_t) { SPN_ARCH_X64, it->os, SPN_ABI_GNU }, (spn_path_t) { .sub = sp_cstr_as_str(it->sdk) });
  }
  spn_zig_stub_t link = {
    .kind = it->link.kind,
    .lang = it->link.lang,
    .system_libs = sp_da_new(mem, sp_str_t),
  };
  u32 libs = 0;
  sp_carr_detect_len(it->link.libs, libs, it->link.libs[libs]);
  sp_for(b, libs) {
    sp_da_push(link.system_libs, sp_cstr_as_str(it->link.libs[b]));
  }

  spn_zig_stub_t stub = spn_zig_stub_canonical(mem, &profile, link);
  sp_expect_eq(t, stub.kind, it->link.kind);
  sp_expect_eq(t, stub.lang, it->link.lang);
  sp_expect_eq(t, stub.linkage, it->expect.linkage);

  u32 expected = 0;
  sp_carr_detect_len(it->expect.libs, expected, it->expect.libs[expected]);
  sp_must_eq(t, sp_da_size(stub.system_libs), expected);
  sp_for(b, expected) {
    sp_test_kv(t, "lib", sp_fmt(mem, "{}", sp_fmt_uint(b)).value);
    sp_expect_str_eq_c(t, stub.system_libs[b], it->expect.libs[b]);
  }

  sp_str_t name = spn_zig_stub_name(mem, sp_str_lit("T"), it->sanitizers, &stub);
  sp_expect_str_eq_c(t, name, it->expect.name);

  return SP_OK;
}
