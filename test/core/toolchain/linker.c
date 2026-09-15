#include "toolchain.h"

typedef struct {
  const c8* name;
  spn_triple_t target;
  spn_ld_dialect_t expect;
} dialect_t;

static const dialect_t dialect_tests [] = {
  { "linux_gnu",    HOST_X64_LINUX,      SPN_LD_DIALECT_GNU },
  { "linux_musl",   HOST_X64_LINUX_MUSL, SPN_LD_DIALECT_GNU },
  { "freestanding", TARGET_X64_BARE,     SPN_LD_DIALECT_GNU },
  { "windows_gnu",  TARGET_WIN_GNU,      SPN_LD_DIALECT_GNU },
  { "windows_msvc", TARGET_WIN_MSVC,     SPN_LD_DIALECT_LINK },
  { "macos",        HOST_ARM_MACOS,      SPN_LD_DIALECT_DARWIN },
  { "wasi",         TARGET_WASM,         SPN_LD_DIALECT_WASM },
};

sp_test_each(linker, dialect, dialect_t, dialect_tests) {
  sp_expect_eq(t, (u32)it->expect, (u32)spn_ld_dialect(it->target));
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_ld_dialect_t dialect;
  bool expect;
} static_t;

static const static_t static_tests [] = {
  { "gnu",    SPN_LD_DIALECT_GNU,    true },
  { "link",   SPN_LD_DIALECT_LINK,   false },
  { "darwin", SPN_LD_DIALECT_DARWIN, false },
  { "wasm",   SPN_LD_DIALECT_WASM,   false },
};

sp_test_each(linker, static, static_t, static_tests) {
  sp_expect_eq(t, it->expect, spn_ld_static(it->dialect));
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
  spn_linking_t linking;
} expect_t;

typedef struct {
  const c8* name;
  spn_triple_t target;
  spn_linking_t request;
  expect_t expect;
} linking_t;

static const linking_t linking_tests [] = {
  { "gnu_defaults",        HOST_X64_LINUX,        .expect = { .linking = { SPN_LIB_KIND_SHARED, SPN_RUNTIME_SHARED, SPN_RUNTIME_SHARED } } },
  { "musl_defaults",       HOST_X64_LINUX_MUSL,   .expect = { .linking = { SPN_LIB_KIND_STATIC, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC } } },
  { "msvc_defaults",       TARGET_WIN_MSVC,       .expect = { .linking = { SPN_LIB_KIND_SHARED, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC } } },
  { "mingw_defaults",      TARGET_WIN_GNU,        .expect = { .linking = { SPN_LIB_KIND_SHARED, SPN_RUNTIME_STATIC, SPN_RUNTIME_SHARED } } },
  { "apple_defaults",      HOST_ARM_MACOS,        .expect = { .linking = { SPN_LIB_KIND_SHARED, SPN_RUNTIME_SHARED, SPN_RUNTIME_SHARED } } },
  { "wasi_defaults",       TARGET_WASM,           .expect = { .linking = { SPN_LIB_KIND_STATIC, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC } } },
  { "bare_defaults",       TARGET_X64_BARE,       .expect = { .linking = { SPN_LIB_KIND_STATIC, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC } } },
  { "elf_defaults",        TARGET_ARM_ELF,        .expect = { .linking = { SPN_LIB_KIND_STATIC, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC } } },
  { "linux_none_defaults", TARGET_X64_LINUX_NONE, .expect = { .linking = { SPN_LIB_KIND_STATIC, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC } } },

  { "musl_shared_runtime_takes_a_loader",     HOST_X64_LINUX_MUSL, .request = { .runtime = SPN_RUNTIME_SHARED },                                  .expect = { .linking = { SPN_LIB_KIND_SHARED, SPN_RUNTIME_SHARED, SPN_RUNTIME_SHARED } } },
  { "gnu_static_runtime_keeps_shared_libc",   HOST_X64_LINUX,      .request = { .runtime = SPN_RUNTIME_STATIC },                                  .expect = { .linking = { SPN_LIB_KIND_SHARED, SPN_RUNTIME_STATIC, SPN_RUNTIME_SHARED } } },
  { "gnu_static_deps_and_runtime_keep_libc",  HOST_X64_LINUX,      .request = { .linkage = SPN_LIB_KIND_STATIC, .runtime = SPN_RUNTIME_STATIC }, .expect = { .linking = { SPN_LIB_KIND_STATIC, SPN_RUNTIME_STATIC, SPN_RUNTIME_SHARED } } },
  { "musl_static_libc_derives_the_rest",      HOST_X64_LINUX_MUSL, .request = { .libc = SPN_RUNTIME_STATIC },                                     .expect = { .linking = { SPN_LIB_KIND_STATIC, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC } } },
  { "msvc_static_libc_keeps_a_loader",        TARGET_WIN_MSVC,     .request = { .libc = SPN_RUNTIME_STATIC },                                     .expect = { .linking = { SPN_LIB_KIND_SHARED, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC } } },
  { "msvc_shared_deps_with_static_runtime",   TARGET_WIN_MSVC,     .request = { .linkage = SPN_LIB_KIND_SHARED, .runtime = SPN_RUNTIME_STATIC }, .expect = { .linking = { SPN_LIB_KIND_SHARED, SPN_RUNTIME_STATIC, SPN_RUNTIME_STATIC } } },

  { "musl_static_libc_shared_runtime", HOST_X64_LINUX_MUSL, .request = { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_STATIC }, .expect = { .refusal = SPN_LINKING_REFUSAL_SHARED_RUNTIME } },

  { "no_loader",                     TARGET_WASM,     .request = { .linkage = SPN_LIB_KIND_SHARED },                                .expect = { .refusal = SPN_LINKING_REFUSAL_NO_LOADER } },
  { "os_libc",                       HOST_X64_LINUX,  .request = { .libc = SPN_RUNTIME_STATIC },                                    .expect = { .refusal = SPN_LINKING_REFUSAL_OS_LIBC } },
  { "os_runtime",                    HOST_ARM_MACOS,  .request = { .runtime = SPN_RUNTIME_STATIC },                                 .expect = { .refusal = SPN_LINKING_REFUSAL_OS_RUNTIME } },
  { "hybrid_crt",                    TARGET_WIN_MSVC, .request = { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_SHARED },     .expect = { .refusal = SPN_LINKING_REFUSAL_HYBRID_CRT } },
  { "shared_deps",                   HOST_X64_LINUX_MUSL, .request = { .linkage = SPN_LIB_KIND_SHARED, .libc = SPN_RUNTIME_STATIC }, .expect = { .refusal = SPN_LINKING_REFUSAL_SHARED_DEPS } },
  { "no_loader_before_shared_runtime", TARGET_WASM,   .request = { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_STATIC },     .expect = { .refusal = SPN_LINKING_REFUSAL_NO_LOADER } },
  { "os_libc_before_os_runtime",     HOST_ARM_MACOS,  .request = { .runtime = SPN_RUNTIME_STATIC, .libc = SPN_RUNTIME_STATIC },     .expect = { .refusal = SPN_LINKING_REFUSAL_OS_LIBC } },
  { "os_libc_before_shared_runtime", HOST_X64_LINUX,  .request = { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_STATIC },     .expect = { .refusal = SPN_LINKING_REFUSAL_OS_LIBC } },
  { "msvc_static_libc_shared_runtime_before_hybrid_crt", TARGET_WIN_MSVC, .request = { .runtime = SPN_RUNTIME_SHARED, .libc = SPN_RUNTIME_STATIC }, .expect = { .refusal = SPN_LINKING_REFUSAL_SHARED_RUNTIME } },
};

sp_test_each(linker, linking, linking_t, linking_tests) {
  spn_linking_t linking = sp_zero;
  sp_must_eq(t, (u32)it->expect.refusal, (u32)spn_ld_linking(it->target, it->request, &linking));
  sp_expect_eq(t, (u32)it->expect.linking.linkage, (u32)linking.linkage);
  sp_expect_eq(t, (u32)it->expect.linking.runtime, (u32)linking.runtime);
  sp_expect_eq(t, (u32)it->expect.linking.libc, (u32)linking.libc);
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_ld_family_t family;
  spn_format_t format;
  bool expect;
} scripts_t;

static const scripts_t scripts_tests [] = {
  { "gnu_elf",    SPN_LD_FAMILY_GNU,  SPN_FORMAT_ELF,   true },
  { "gnu_coff",   SPN_LD_FAMILY_GNU,  SPN_FORMAT_COFF,  true },
  { "lld_elf",    SPN_LD_FAMILY_LLD,  SPN_FORMAT_ELF,   true },
  { "lld_coff",   SPN_LD_FAMILY_LLD,  SPN_FORMAT_COFF,  false },
  { "lld_macho",  SPN_LD_FAMILY_LLD,  SPN_FORMAT_MACHO, false },
  { "lld_wasm",   SPN_LD_FAMILY_LLD,  SPN_FORMAT_WASM,  false },
  { "ld64_macho", SPN_LD_FAMILY_LD64, SPN_FORMAT_MACHO, false },
  { "msvc_coff",  SPN_LD_FAMILY_MSVC, SPN_FORMAT_COFF,  false },
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
  spn_cc_driver_t driver;
  spn_triple_t target;
  spn_ld_family_t expect;
} native_t;

static const native_t native_tests [] = {
  { "gcc_linux",   SPN_CC_DRIVER_GCC,   HOST_X64_LINUX,  SPN_LD_FAMILY_GNU },
  { "gcc_mingw",   SPN_CC_DRIVER_GCC,   TARGET_WIN_GNU,  SPN_LD_FAMILY_GNU },
  { "gcc_macos",   SPN_CC_DRIVER_GCC,   HOST_ARM_MACOS,  SPN_LD_FAMILY_LD64 },
  { "clang_msvc",  SPN_CC_DRIVER_CLANG, TARGET_WIN_MSVC, SPN_LD_FAMILY_MSVC },
  { "clang_wasi",  SPN_CC_DRIVER_CLANG, TARGET_WASM,     SPN_LD_FAMILY_LLD },
  { "msvc_msvc",   SPN_CC_DRIVER_MSVC,  TARGET_WIN_MSVC, SPN_LD_FAMILY_MSVC },
  { "zig_linux",   SPN_CC_DRIVER_ZIG,   HOST_X64_LINUX,  SPN_LD_FAMILY_LLD },
  { "zig_macos",   SPN_CC_DRIVER_ZIG,   HOST_ARM_MACOS,  SPN_LD_FAMILY_LLD },
};

sp_test_each(linker, native, native_t, native_tests) {
  sp_expect_eq(t, (u32)it->expect, (u32)spn_ld_native(it->driver, it->target));
  return SP_OK;
}
