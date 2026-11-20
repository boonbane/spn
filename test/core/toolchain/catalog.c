#include "toolchain.h"

#define CATALOG_MAX_ADDS 2
#define CATALOG_MAX_ORDER 4

typedef struct {
  const c8* name;
  fixture_launcher_t compiler;
} add_t;

typedef struct {
  const c8* order [CATALOG_MAX_ORDER];
  const c8* toolchain;
  test_arg_t compiler;
} add_expect_t;

typedef struct {
  const c8* name;
  const c8* file;
  add_t adds [CATALOG_MAX_ADDS];
  add_expect_t expect;
} add_test_t;

typedef struct {
  fixture_row_t rows [FIXTURE_MAX_TARGETS];
  spn_triple_t unserved [FIXTURE_MAX_TARGETS];
} rows_expect_t;

typedef struct {
  const c8* name;
  const c8* file;
  rows_expect_t expect;
} artifact_test_t;

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  bool host_row;
  spn_triple_t host;
  fixture_sdks_t sdks;
  fixture_target_t targets [FIXTURE_MAX_TARGETS];
  rows_expect_t expect;
} bind_test_t;

typedef struct {
  spn_toolchain_support_kind_t kind;
  const c8* artifact;
} support_expect_t;

typedef struct {
  const c8* name;
  const c8* file;
  spn_triple_t host;
  fixture_sdks_t sdks;
  const c8* toolchain;
  support_expect_t expect;
} support_test_t;

typedef struct {
  spn_triple_t triple;
  spn_runtime_set_t runtimes;
} runtime_row_t;

typedef struct {
  runtime_row_t rows [FIXTURE_MAX_TARGETS];
} runtime_expect_t;

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  spn_triple_t host;
  fixture_sdks_t sdks;
  fixture_target_t targets [FIXTURE_MAX_TARGETS];
  runtime_expect_t expect;
} runtime_test_t;

static const add_test_t add_tests [] = {
  {
    .name = "overrides_by_name",
    .file = "multiple.toml",
    .adds = {
      { .name = "A", .compiler = { .path = "/A" } },
    },
    .expect = {
      .order = { "A", "B" },
      .toolchain = "A",
      .compiler = { .path = "/A" },
    },
  },
  {
    .name = "coexists_with_entries",
    .file = "multiple.toml",
    .adds = {
      { .name = "C", .compiler = { .name = "C" } },
    },
    .expect = {
      .order = { "A", "B", "C" },
    },
  },
};

static const artifact_test_t artifact_tests [] = {
  {
    .name = "relative_sdks_root_under_the_artifact",
    .file = "sdk.toml",
    .expect = {
      .rows = {
        { HOST_ARM_LINUX, { SPN_SDK_SYSROOT, { "store/aa/S/linux", SPN_PATH_ROOT_TOOLCHAIN } } },
        { HOST_ARM_MACOS, { SPN_SDK_MACOS, { "store/aa/S/macos", SPN_PATH_ROOT_TOOLCHAIN } } },
        { TARGET_WASM, { SPN_SDK_SYSROOT, { "store/aa/S/wasi", SPN_PATH_ROOT_TOOLCHAIN } } },
        { TARGET_WIN_GNU, { SPN_SDK_SYSROOT, { "store/aa/S/windows", SPN_PATH_ROOT_TOOLCHAIN } } },
        { TARGET_WIN_MSVC, { SPN_SDK_MSVC, { "store/aa/S/msvc", SPN_PATH_ROOT_TOOLCHAIN } } },
        { HOST_X64_LINUX },
      },
    },
  },
  {
    .name = "absolute_sdk_is_a_host_path",
    .file = "sdk_absolute.toml",
    .expect = { .rows = { { HOST_ARM_LINUX, { SPN_SDK_SYSROOT, { "/S" } } } } },
  },
};

static const bind_test_t bind_tests [] = {
  {
    .name = "gcc_on_linux_targets_host_and_none",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_X64_LINUX,
    .expect = { .rows = { { HOST_X64_LINUX, { SPN_SDK_HOST }, SAN_GCC_LINUX }, { TARGET_X64_BARE }, { TARGET_X64_LINUX_NONE } } },
  },
  {
    .name = "clang_on_linux_targets_host_and_no_bare",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_ARM_LINUX,
    .expect = { .rows = { { HOST_ARM_LINUX, { SPN_SDK_HOST }, SAN_CLANG_LINUX } } },
  },
  {
    .name = "gcc_on_macos_targets_host_only",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_ARM_MACOS,
    .expect = { .rows = { { HOST_ARM_MACOS, .sanitizers = SAN_GCC_MACOS } } },
  },
  {
    .name = "clang_on_macos_without_sdk_targets_host_only",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_ARM_MACOS,
    .expect = { .rows = { { HOST_ARM_MACOS, .sanitizers = SAN_CLANG_MACOS } } },
  },
  {
    .name = "gcc_on_windows_brings_its_libc",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_X64_WINDOWS,
    .expect = { .rows = { { TARGET_WIN_GNU } } },
  },
  {
    .name = "msvc_on_linux_targets_nothing",
    .driver = SPN_CC_DRIVER_MSVC,
    .host = HOST_X64_LINUX,
  },
  {
    .name = "entry_path_wins_over_host",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_ARM_MACOS,
    .sdks = { .macos = { "/H" } },
    .targets = { { HOST_ARM_MACOS, { .path = "/E" } } },
    .expect = { .rows = { { HOST_ARM_MACOS, { SPN_SDK_MACOS, { "/E" } } } } },
  },
  {
    .name = "absent_macos_takes_the_served_host_sdk",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_ARM_MACOS,
    .sdks = { .macos = { "/H" } },
    .targets = { { HOST_ARM_MACOS } },
    .expect = { .rows = { { HOST_ARM_MACOS, { SPN_SDK_MACOS, { "/H" } } } } },
  },
  {
    .name = "absent_macos_without_a_host_sdk_is_kept",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_ARM_MACOS,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .targets = { { HOST_ARM_MACOS } },
    .expect = { .rows = { { HOST_ARM_MACOS } } },
  },
  {
    .name = "absent_msvc_takes_the_served_host_sdk",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_X64_WINDOWS,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .targets = { { TARGET_WIN_MSVC } },
    .expect = { .rows = { { TARGET_WIN_MSVC, { SPN_SDK_MSVC, { "/X" } } } } },
  },
  {
    .name = "absent_msvc_without_a_host_sdk_is_unserved",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_X64_WINDOWS,
    .sdks = { .macos = { "/H" } },
    .targets = { { TARGET_WIN_MSVC } },
    .expect = { .unserved = { TARGET_WIN_MSVC } },
  },
  {
    .name = "msvc_host_row_without_a_host_sdk_is_unserved",
    .driver = SPN_CC_DRIVER_MSVC,
    .host = HOST_X64_WINDOWS,
    .expect = { .unserved = { TARGET_WIN_MSVC } },
  },
  {
    .name = "absent_sysroot_passes_nothing_anywhere",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_X64_LINUX,
    .targets = { { HOST_X64_LINUX }, { HOST_X64_LINUX_MUSL }, { HOST_ARM_LINUX }, { TARGET_WIN_GNU }, { TARGET_WASM } },
    .expect = { .rows = { { HOST_X64_LINUX }, { HOST_X64_LINUX_MUSL }, { HOST_ARM_LINUX }, { TARGET_WIN_GNU }, { TARGET_WASM } } },
  },
  {
    .name = "derived_retargeting_driver_reaches_served_sdks",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_X64_LINUX,
    .sdks = { .macos = { "/H" }, .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .expect = {
      .rows = {
        { HOST_X64_LINUX, { SPN_SDK_HOST }, SAN_CLANG_LINUX },
        { HOST_X64_MACOS, { SPN_SDK_MACOS, { "/H" } } },
        { HOST_ARM_MACOS, { SPN_SDK_MACOS, { "/H" } } },
        { TARGET_WIN_MSVC, { SPN_SDK_MSVC, { "/X" } } },
      },
    },
  },
  {
    .name = "listed_rows_keep_their_sanitizers",
    .driver = SPN_CC_DRIVER_ZIG,
    .host = HOST_X64_LINUX,
    .targets = { { .triple = HOST_X64_LINUX, .sanitizers = SAN_ZIG_UT }, { .triple = TARGET_X64_BARE } },
    .expect = { .rows = { { HOST_X64_LINUX, .sanitizers = SAN_ZIG_UT }, { TARGET_X64_BARE } } },
  },
  {
    .name = "zig_host_row_brings_its_libc",
    .driver = SPN_CC_DRIVER_ZIG,
    .host = HOST_X64_LINUX,
    .expect = { .rows = { { HOST_X64_LINUX }, { TARGET_X64_BARE }, { TARGET_X64_LINUX_NONE } } },
  },
  {
    .name = "host_row_adds_to_a_list",
    .driver = SPN_CC_DRIVER_CLANG,
    .host_row = true,
    .host = HOST_X64_LINUX,
    .targets = { { TARGET_X64_BARE } },
    .expect = { .rows = { { TARGET_X64_BARE }, { HOST_X64_LINUX, { SPN_SDK_HOST }, SAN_CLANG_LINUX } } },
  },
  {
    .name = "listed_row_wins_over_host",
    .driver = SPN_CC_DRIVER_GCC,
    .host_row = true,
    .host = HOST_X64_LINUX,
    .targets = { { HOST_X64_LINUX, { .path = "/S" } } },
    .expect = { .rows = { { HOST_X64_LINUX, { SPN_SDK_SYSROOT, { "/S" } } }, { TARGET_X64_BARE }, { TARGET_X64_LINUX_NONE } } },
  },
  {
    .name = "clang_on_windows_has_no_stock_row",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_X64_WINDOWS,
  },
  {
    .name = "clang_on_windows_reaches_the_served_msvc_sdk",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_X64_WINDOWS,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .expect = { .rows = { { TARGET_WIN_MSVC, { SPN_SDK_MSVC, { "/X" } } } } },
  },
  {
    .name = "macos_host_with_sdk_reaches_both_arches_with_sanitizers",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_ARM_MACOS,
    .sdks = { .macos = { "/H" } },
    .expect = { .rows = { { HOST_ARM_MACOS, { SPN_SDK_MACOS, { "/H" } }, SAN_CLANG_MACOS }, { HOST_X64_MACOS, { SPN_SDK_MACOS, { "/H" } }, SAN_CLANG_MACOS } } },
  },
};

static const support_test_t support_tests [] = {
  {
    .name = "local_without_hosts_supports_any_host",
    .file = "local.toml",
    .host = HOST_ARM_MACOS,
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_LOCAL },
  },
  {
    .name = "local_with_hosts_supports_listed_host",
    .file = "restricted.toml",
    .host = HOST_X64_LINUX,
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_LOCAL },
  },
  {
    .name = "local_with_hosts_rejects_other_host",
    .file = "restricted.toml",
    .host = HOST_ARM_MACOS,
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_NONE },
  },
  {
    .name = "distribution_resolves_host_artifact",
    .file = "distribution.toml",
    .host = HOST_X64_LINUX,
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_ARTIFACT, .artifact = "https://example.com/linux.tar.xz" },
  },
  {
    .name = "distribution_resolves_per_host",
    .file = "distribution.toml",
    .host = HOST_ARM_MACOS,
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_ARTIFACT, .artifact = "https://example.com/macos.tar.xz" },
  },
  {
    .name = "distribution_ignores_host_abi",
    .file = "distribution.toml",
    .host = HOST_X64_LINUX_MUSL,
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_ARTIFACT, .artifact = "https://example.com/linux.tar.xz" },
  },
  {
    .name = "distribution_rejects_unlisted_host",
    .file = "distribution.toml",
    .host = HOST_ARM_LINUX,
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_NONE },
  },
  {
    .name = "detected_with_an_install",
    .file = "detected.toml",
    .host = HOST_X64_WINDOWS,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_DETECTED },
  },
  {
    .name = "detected_with_only_a_foreign_arch_install",
    .file = "detected.toml",
    .host = HOST_X64_WINDOWS,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_ARM64 } } },
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_DETECTED },
  },
  {
    .name = "detected_without_an_install_is_none",
    .file = "detected.toml",
    .host = HOST_X64_WINDOWS,
    .toolchain = "A",
    .expect = { .kind = SPN_TOOLCHAIN_SUPPORT_NONE },
  },
};

static const runtime_test_t runtime_tests [] = {
  {
    .name = "written_row_has_what_it_declares",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_X64_LINUX,
    .targets = { { .triple = HOST_X64_LINUX, .runtimes = rt_shared }, { .triple = HOST_X64_LINUX_MUSL, .runtimes = rt_both } },
    .expect = { .rows = { { HOST_X64_LINUX, rt_shared }, { HOST_X64_LINUX_MUSL, rt_both } } },
  },
  {
    .name = "written_row_gets_the_static_floor",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_X64_LINUX,
    .targets = { { HOST_X64_LINUX }, { HOST_X64_LINUX_MUSL }, { TARGET_WIN_GNU }, { TARGET_WIN_MSVC, { .path = "/X" } }, { TARGET_WASM }, { TARGET_X64_BARE } },
    .expect = { .rows = { { HOST_X64_LINUX, rt_static }, { HOST_X64_LINUX_MUSL, rt_static }, { TARGET_WIN_GNU, rt_static }, { TARGET_WIN_MSVC, rt_static }, { TARGET_WASM, rt_static }, { TARGET_X64_BARE, rt_static } } },
  },
  {
    .name = "written_macos_row_gets_the_shared_floor",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_X64_LINUX,
    .targets = { { HOST_ARM_MACOS, { .path = "/S" } } },
    .expect = { .rows = { { HOST_ARM_MACOS, rt_shared } } },
  },
  {
    .name = "written_row_on_a_detected_msvc_sdk_gets_the_static_floor",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_X64_WINDOWS,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .targets = { { TARGET_WIN_MSVC } },
    .expect = { .rows = { { TARGET_WIN_MSVC, rt_static } } },
  },
  {
    .name = "host_row_on_linux_has_both",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_X64_LINUX,
    .expect = { .rows = { { HOST_X64_LINUX, rt_both }, { TARGET_X64_BARE, rt_static }, { TARGET_X64_LINUX_NONE, rt_static } } },
  },
  {
    .name = "host_row_on_macos_is_shared",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_ARM_MACOS,
    .sdks = { .macos = { "/H" } },
    .expect = { .rows = { { HOST_ARM_MACOS, rt_shared }, { HOST_X64_MACOS, rt_shared } } },
  },
  {
    .name = "gcc_host_row_on_windows_is_static",
    .driver = SPN_CC_DRIVER_GCC,
    .host = HOST_X64_WINDOWS,
    .expect = { .rows = { { TARGET_WIN_GNU, rt_static } } },
  },
  {
    .name = "clang_host_row_on_a_detected_msvc_sdk_has_both",
    .driver = SPN_CC_DRIVER_CLANG,
    .host = HOST_X64_WINDOWS,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .expect = { .rows = { { TARGET_WIN_MSVC, rt_both } } },
  },
  {
    .name = "msvc_host_row_has_both",
    .driver = SPN_CC_DRIVER_MSVC,
    .host = HOST_X64_WINDOWS,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .expect = { .rows = { { TARGET_WIN_MSVC, rt_both } } },
  },
  {
    .name = "zig_is_static_off_msvc",
    .driver = SPN_CC_DRIVER_ZIG,
    .host = HOST_X64_LINUX,
    .targets = { { HOST_X64_LINUX }, { HOST_X64_LINUX_MUSL }, { HOST_ARM_MACOS }, { TARGET_WIN_GNU }, { TARGET_WASM } },
    .expect = { .rows = { { HOST_X64_LINUX, rt_static }, { HOST_X64_LINUX_MUSL, rt_static }, { HOST_ARM_MACOS, rt_static }, { TARGET_WIN_GNU, rt_static }, { TARGET_WASM, rt_static } } },
  },
  {
    .name = "zig_is_static_whatever_a_row_says",
    .driver = SPN_CC_DRIVER_ZIG,
    .host = HOST_X64_LINUX,
    .targets = { { .triple = HOST_X64_LINUX, .runtimes = rt_both } },
    .expect = { .rows = { { HOST_X64_LINUX, rt_static } } },
  },
  {
    .name = "zig_host_rows_are_static_off_msvc",
    .driver = SPN_CC_DRIVER_ZIG,
    .host = HOST_ARM_MACOS,
    .sdks = { .macos = { "/H" } },
    .expect = { .rows = { { HOST_ARM_MACOS, rt_static }, { HOST_X64_MACOS, rt_static } } },
  },
  {
    .name = "zig_host_row_on_a_detected_msvc_sdk_has_both",
    .driver = SPN_CC_DRIVER_ZIG,
    .host = HOST_X64_WINDOWS,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .expect = { .rows = { { TARGET_WIN_GNU, rt_static }, { TARGET_WIN_MSVC, rt_both } } },
  },
  {
    .name = "zig_msvc_row_has_what_it_declares",
    .driver = SPN_CC_DRIVER_ZIG,
    .host = HOST_X64_LINUX,
    .targets = { { .triple = TARGET_WIN_MSVC, .sdk = { .path = "/X" } }, { .triple = TARGET_ARM_WIN_MSVC, .sdk = { .path = "/X" }, .runtimes = rt_both } },
    .expect = { .rows = { { TARGET_WIN_MSVC, rt_static }, { TARGET_ARM_WIN_MSVC, rt_both } } },
  },
};

sp_test_each(catalog, add, add_test_t, add_tests, .setup = spn_test_ctx_setup) {
  spn_toolchain_catalog_t catalog = sp_zero;
  if (fixture_catalog(t, &catalog, it->file, (spn_triple_t) HOST_X64_LINUX, sp_zero_struct(spn_sdk_host_t))) {
    return SP_ERR;
  }

  u32 adds = 0;
  sp_carr_detect_len(it->adds, adds, it->adds[adds].name);
  sp_for(at, adds) {
    spn_toolchain_catalog_add(&catalog, fixture_local_toolchain(it->adds[at].name, it->adds[at].compiler));
  }

  u32 order = 0;
  sp_carr_detect_len(it->expect.order, order, it->expect.order[order]);
  sp_must_eq(t, order, (u32)si_om_size(catalog.entries));
  sp_for(at, order) {
    sp_expect_str_eq_c(t, si_om_at(catalog.entries, at)->name.str, it->expect.order[at]);
  }

  if (!it->expect.toolchain) {
    return SP_OK;
  }
  spn_toolchain_info_t* info = spn_toolchain_catalog_get(&catalog, spn_intern_cstr(it->expect.toolchain).id);
  sp_must(t, info);
  return test_check_arg(t, info->compiler.program, it->expect.compiler);
}

sp_test_each(catalog, artifact, artifact_test_t, artifact_tests, .setup = spn_test_ctx_setup) {
  spn_toolchain_catalog_t catalog = sp_zero;
  if (fixture_catalog(t, &catalog, it->file, (spn_triple_t) HOST_X64_LINUX, sp_zero_struct(spn_sdk_host_t))) {
    return SP_ERR;
  }

  spn_toolchain_info_t* info = spn_toolchain_catalog_get(&catalog, spn_intern(sp_str_lit("A")).id);
  sp_must(t, info);
  return fixture_check_rows(t, info->rows, it->expect.rows);
}

sp_test_each(catalog, bind, bind_test_t, bind_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_toolchain_decl_t toolchain = fixture_local_toolchain("A", (fixture_launcher_t) { .name = "cc" });
  toolchain.driver = it->driver;
  toolchain.targets = sp_da_new(mem, spn_toolchain_target_t);
  sp_carr_for(it->targets, at) {
    if (fixture_target_empty(it->targets[at])) {
      break;
    }
    sp_da_push(toolchain.targets, fixture_target(it->targets[at]));
  }
  toolchain.host_row = it->host_row || sp_da_empty(toolchain.targets);

  spn_toolchain_catalog_t catalog = sp_zero;
  spn_toolchain_catalog_init(&catalog, it->host, fixture_sdks(mem, it->sdks), mem);
  spn_toolchain_catalog_add(&catalog, toolchain);

  spn_toolchain_info_t* info = spn_toolchain_catalog_get(&catalog, spn_intern(sp_str_lit("A")).id);
  sp_must(t, info);
  if (fixture_check_rows(t, info->rows, it->expect.rows)) {
    return SP_ERR;
  }
  u32 unserved = 0;
  sp_carr_detect_len(it->expect.unserved, unserved, !fixture_triple_empty(it->expect.unserved[unserved]));
  return fixture_check_triples(t, info->unserved, it->expect.unserved, unserved);
}

sp_test_each(catalog, runtimes, runtime_test_t, runtime_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_toolchain_decl_t toolchain = fixture_local_toolchain("A", (fixture_launcher_t) { .name = "cc" });
  toolchain.driver = it->driver;
  toolchain.targets = sp_da_new(mem, spn_toolchain_target_t);
  u32 declared = 0;
  sp_carr_detect_len(it->targets, declared, !fixture_target_empty(it->targets[declared]));
  sp_for(at, declared) {
    sp_da_push(toolchain.targets, fixture_target(it->targets[at]));
  }
  toolchain.host_row = !declared;

  spn_toolchain_catalog_t catalog = sp_zero;
  spn_toolchain_catalog_init(&catalog, it->host, fixture_sdks(mem, it->sdks), mem);
  spn_toolchain_catalog_add(&catalog, toolchain);

  spn_toolchain_info_t* info = spn_toolchain_catalog_get(&catalog, toolchain.id);
  sp_must(t, info);
  u32 rows = 0;
  sp_carr_detect_len(it->expect.rows, rows, !fixture_triple_empty(it->expect.rows[rows].triple));
  sp_must_eq(t, rows, (u32)sp_da_size(info->rows));
  sp_for(at, rows) {
    const spn_toolchain_row_t* row = spn_toolchain_row(info, it->expect.rows[at].triple);
    sp_must(t, row);
    sp_expect_eq(t, it->expect.rows[at].runtimes, row->runtimes);
  }
  return SP_OK;
}

sp_test_each(catalog, support, support_test_t, support_tests, .setup = spn_test_ctx_setup) {
  spn_toolchain_catalog_t catalog = sp_zero;
  if (fixture_catalog(t, &catalog, it->file, it->host, fixture_sdks(sp_test_arena(t), it->sdks))) {
    return SP_ERR;
  }

  spn_toolchain_info_t* info = spn_toolchain_catalog_get(&catalog, spn_intern_cstr(it->toolchain).id);
  sp_must(t, info);

  sp_must_eq(t, (u32)it->expect.kind, (u32)info->support.kind);
  if (info->support.kind == SPN_TOOLCHAIN_SUPPORT_ARTIFACT) {
    sp_expect_str_eq_c(t, info->support.artifact.url, it->expect.artifact);
  }
  return SP_OK;
}
