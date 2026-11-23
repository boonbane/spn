#include "toolchain.h"

typedef struct {
  const c8* name;
  spn_triple_t target;
  spn_ld_dialect_t expect;
} dialect_t;

static const dialect_t dialect_tests [] = {
  { "linux_gnu",    HOST_X64_LINUX,  SPN_LD_DIALECT_GNU },
  { "windows_gnu",  TARGET_WIN_GNU,  SPN_LD_DIALECT_GNU },
  { "windows_msvc", TARGET_WIN_MSVC, SPN_LD_DIALECT_LINK },
  { "macos",        HOST_ARM_MACOS,  SPN_LD_DIALECT_DARWIN },
  { "wasi",         TARGET_WASM,     SPN_LD_DIALECT_WASM },
};

sp_test_each(linker, dialect, dialect_t, dialect_tests) {
  sp_expect_eq(t, (u32)it->expect, (u32)si_ld_dialect(it->target));
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_triple_t target;
  spn_runtime_t libc;
  bool expect;
} loader_t;

static const loader_t loader_tests [] = {
  { "gnu_shared_libc",  HOST_X64_LINUX,      SPN_RUNTIME_SHARED, true },
  { "musl_static_libc", HOST_X64_LINUX_MUSL, SPN_RUNTIME_STATIC, false },
  { "msvc_static_libc", TARGET_WIN_MSVC,     SPN_RUNTIME_STATIC, true },
  { "macos",            HOST_ARM_MACOS,      SPN_RUNTIME_SHARED, true },
  { "wasi",             TARGET_WASM,         SPN_RUNTIME_STATIC, false },
  { "bare",             TARGET_X64_BARE,     SPN_RUNTIME_STATIC, false },
};

sp_test_each(linker, loader, loader_t, loader_tests) {
  sp_expect_eq(t, it->expect, spn_ld_loader(it->target, (spn_linking_t) { .libc = it->libc }));
  return SP_OK;
}

typedef struct {
  spn_linking_refusal_t refusal;
  spn_linking_t linkings [si_linkings_max];
} expect_t;

typedef struct {
  const c8* name;
  spn_triple_t target;
  spn_linking_t request;
  spn_linkage_t demand;
  expect_t expect;
} linking_t;

#define shared_shared_shared { SPN_LIB_KIND_SHARED, SPN_RUNTIME_SHARED, SPN_RUNTIME_SHARED }
#define shared_static_shared { SPN_LIB_KIND_SHARED, SPN_RUNTIME_STATIC, SPN_RUNTIME_SHARED }
#define shared_static_static { SPN_LIB_KIND_SHARED, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC }
#define static_shared_shared { SPN_LIB_KIND_STATIC, SPN_RUNTIME_SHARED, SPN_RUNTIME_SHARED }
#define static_static_shared { SPN_LIB_KIND_STATIC, SPN_RUNTIME_STATIC, SPN_RUNTIME_SHARED }
#define static_static_static { SPN_LIB_KIND_STATIC, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC }

static const linking_t linking_tests [] = {
  { "gnu_defaults",        HOST_X64_LINUX,        .expect = { .linkings = { shared_shared_shared, shared_static_shared } } },
  { "musl_defaults",       HOST_X64_LINUX_MUSL,   .expect = { .linkings = { static_static_static, shared_static_shared, shared_shared_shared } } },
  { "msvc_defaults",       TARGET_WIN_MSVC,       .expect = { .linkings = { shared_static_static, shared_shared_shared } } },
  { "mingw_defaults",      TARGET_WIN_GNU,        .expect = { .linkings = { shared_static_shared, shared_shared_shared } } },
  { "apple_defaults",      HOST_ARM_MACOS,        .expect = { .linkings = { shared_shared_shared, shared_static_shared } } },
  { "wasi_defaults",       TARGET_WASM,           .expect = { .linkings = { static_static_static } } },
  { "bare_defaults",       TARGET_X64_BARE,       .expect = { .linkings = { static_static_static } } },
  { "elf_defaults",        TARGET_ARM_ELF,        .expect = { .linkings = { static_static_static } } },
  { "linux_none_defaults", TARGET_X64_LINUX_NONE, .expect = { .linkings = { static_static_static } } },

  { "musl_shared_runtime_takes_a_loader",    HOST_X64_LINUX_MUSL, .request = { .runtime = SPN_RUNTIME_SHARED },                                  .expect = { .linkings = { shared_shared_shared } } },
  { "gnu_static_runtime_keeps_shared_libc",  HOST_X64_LINUX,      .request = { .runtime = SPN_RUNTIME_STATIC },                                  .expect = { .linkings = { shared_static_shared } } },
  { "apple_static_runtime_keeps_shared_libc", HOST_ARM_MACOS,     .request = { .runtime = SPN_RUNTIME_STATIC },                                  .expect = { .linkings = { shared_static_shared } } },
  { "gnu_static_deps_keep_every_runtime",    HOST_X64_LINUX,      .request = { .linkage = SPN_LIB_KIND_STATIC },                                 .expect = { .linkings = { static_shared_shared, static_static_shared } } },
  { "gnu_static_deps_and_runtime_keep_libc", HOST_X64_LINUX,      .request = { .linkage = SPN_LIB_KIND_STATIC, .runtime = SPN_RUNTIME_STATIC }, .expect = { .linkings = { static_static_shared } } },
  { "musl_static_libc_derives_the_rest",     HOST_X64_LINUX_MUSL, .request = { .libc = SPN_RUNTIME_STATIC },                                     .expect = { .linkings = { static_static_static } } },
  { "musl_shared_libc_keeps_every_runtime",  HOST_X64_LINUX_MUSL, .request = { .libc = SPN_RUNTIME_SHARED },                                     .expect = { .linkings = { shared_static_shared, shared_shared_shared } } },
  { "msvc_static_libc_keeps_a_loader",       TARGET_WIN_MSVC,     .request = { .libc = SPN_RUNTIME_STATIC },                                     .expect = { .linkings = { shared_static_static } } },
  { "msvc_shared_libc_takes_the_runtime",    TARGET_WIN_MSVC,     .request = { .libc = SPN_RUNTIME_SHARED },                                     .expect = { .linkings = { shared_shared_shared } } },

  { "demand_prefers_a_loader",          HOST_X64_LINUX_MUSL, .demand = SPN_LIB_KIND_SHARED,                                              .expect = { .linkings = { shared_static_shared, shared_shared_shared, static_static_static } } },
  { "demand_yields_to_static_libc",     HOST_X64_LINUX_MUSL, .request = { .libc = SPN_RUNTIME_STATIC }, .demand = SPN_LIB_KIND_SHARED,    .expect = { .linkings = { static_static_static } } },
  { "demand_keeps_static_deps",         HOST_X64_LINUX_MUSL, .request = { .linkage = SPN_LIB_KIND_STATIC }, .demand = SPN_LIB_KIND_SHARED, .expect = { .linkings = { static_static_shared, static_shared_shared, static_static_static } } },
  { "demand_without_a_loader_is_moot",  TARGET_WASM,         .demand = SPN_LIB_KIND_SHARED,                                              .expect = { .linkings = { static_static_static } } },

  { "musl_static_libc_shared_runtime", HOST_X64_LINUX_MUSL, .request = { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_STATIC }, .expect = { .refusal = SPN_LINKING_REFUSAL_SHARED_RUNTIME } },

  { "no_loader",                     TARGET_WASM,     .request = { .runtime = SPN_RUNTIME_SHARED },                                 .expect = { .refusal = SPN_LINKING_REFUSAL_NO_LOADER } },
  { "os_libc",                       HOST_X64_LINUX,  .request = { .libc = SPN_RUNTIME_STATIC },                                    .expect = { .refusal = SPN_LINKING_REFUSAL_OS_LIBC } },
  { "hybrid_crt",                    TARGET_WIN_MSVC, .request = { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_SHARED },     .expect = { .refusal = SPN_LINKING_REFUSAL_HYBRID_CRT } },
  { "no_loader_before_shared_runtime", TARGET_WASM,   .request = { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_STATIC },     .expect = { .refusal = SPN_LINKING_REFUSAL_NO_LOADER } },
  { "os_libc_with_a_static_runtime", HOST_ARM_MACOS,  .request = { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_STATIC },     .expect = { .refusal = SPN_LINKING_REFUSAL_OS_LIBC } },
  { "os_libc_before_shared_runtime", HOST_X64_LINUX,  .request = { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_STATIC },     .expect = { .refusal = SPN_LINKING_REFUSAL_OS_LIBC } },
  { "msvc_static_libc_shared_runtime_before_hybrid_crt", TARGET_WIN_MSVC, .request = { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_STATIC }, .expect = { .refusal = SPN_LINKING_REFUSAL_SHARED_RUNTIME } },
};

sp_test_each(linker, linkings, linking_t, linking_tests) {
  spn_linkings_t linkings = sp_zero;
  sp_must_eq(t, (u32)it->expect.refusal, (u32)si_ld_linkings(it->target, it->request, it->demand, &linkings));
  u32 count = 0;
  sp_carr_detect_len(it->expect.linkings, count, it->expect.linkings[count].linkage);
  sp_must_eq(t, count, linkings.count);
  sp_for(at, count) {
    sp_expect_eq(t, (u32)it->expect.linkings[at].linkage, (u32)linkings.items[at].linkage);
    sp_expect_eq(t, (u32)it->expect.linkings[at].runtime, (u32)linkings.items[at].runtime);
    sp_expect_eq(t, (u32)it->expect.linkings[at].libc, (u32)linkings.items[at].libc);
  }
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_ld_family_t family;
  spn_obj_format_t format;
  bool expect;
} scripts_t;

static const scripts_t scripts_tests [] = {
  { "gnu_elf",    SPN_LD_FAMILY_GNU,  SPN_OBJ_ELF,   true },
  { "gnu_coff",   SPN_LD_FAMILY_GNU,  SPN_OBJ_COFF,  true },
  { "lld_elf",    SPN_LD_FAMILY_LLD,  SPN_OBJ_ELF,   true },
  { "lld_coff",   SPN_LD_FAMILY_LLD,  SPN_OBJ_COFF,  false },
  { "msvc_coff",  SPN_LD_FAMILY_MSVC, SPN_OBJ_COFF,  false },
};

sp_test_each(linker, scripts, scripts_t, scripts_tests) {
  sp_expect_eq(t, it->expect, spn_ld_scripts(it->family, it->format));
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  spn_ld_family_t declared;
  bool expect;
} accepts_t;

static const accepts_t accepts_tests [] = {
  { "nothing_declared",       SPN_CC_DRIVER_GCC,   SPN_LD_FAMILY_NONE, true },
  { "gcc_declares_lld",       SPN_CC_DRIVER_GCC,   SPN_LD_FAMILY_LLD,  true },
  { "clang_declares_lld",     SPN_CC_DRIVER_CLANG, SPN_LD_FAMILY_LLD,  true },
  { "zig_rejects_lld",        SPN_CC_DRIVER_ZIG,   SPN_LD_FAMILY_LLD,  false },
  { "msvc_rejects_lld",       SPN_CC_DRIVER_MSVC,  SPN_LD_FAMILY_LLD,  false },
  { "native_is_not_declared", SPN_CC_DRIVER_GCC,   SPN_LD_FAMILY_GNU,  false },
  { "foreign_is_rejected",    SPN_CC_DRIVER_CLANG, SPN_LD_FAMILY_LD64, false },
};

sp_test_each(linker, accepts, accepts_t, accepts_tests) {
  sp_expect_eq(t, it->expect, spn_ld_accepts(it->driver, it->declared));
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_triple_t target;
  bool expect;
} dynamic_t;

static const dynamic_t dynamic_tests [] = {
  { "linux",            HOST_X64_LINUX,  true },
  { "freestanding_elf", TARGET_ARM_ELF },
  { "linux_none",       TARGET_X64_LINUX_NONE },
};

sp_test_each(linker, dynamic, dynamic_t, dynamic_tests) {
  sp_expect_eq(t, it->expect, spn_ld_dynamic(it->target));
  return SP_OK;
}
