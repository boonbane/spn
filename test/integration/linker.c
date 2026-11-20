#include "harness.h"

static const test_t tests [] = {
  {
    .name = "script_sets_entry",
    .project = "test/integration/fixtures/linker/script",
    .copy = { "main.ld" },
    .when.target = SPN_TEST_ARCH "-freestanding-none",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", SPN_TEST_ARCH "-freestanding-none" } } },
      { .kind = ACTION_VERIFY_ELF_ENTRY, .verify_elf_entry = { target_exe("main", SPN_TEST_ARCH "-freestanding-none"), 0x400000 } },
    },
  },
  {
    .name = "script_unsupported_by_lld_on_windows_gnu",
    .project = "test/integration/fixtures/linker/unsupported",
    .copy = { "main.ld" },
    .when = { .target = "x86_64-windows-gnu", .linker = SPN_LD_FAMILY_LLD },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-gnu" }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED },
    },
  },
  {
    .name = "link_flags_gated_on_linker_apply",
    .project = "test/integration/fixtures/linker/gated",
    .when.linker = SPN_LD_FAMILY_LLD,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_LINK_FAILED } },
    },
  },
  {
    .name = "link_flags_gated_on_linker_skip",
    .project = "test/integration/fixtures/linker/gated",
    .when.linker_not = SPN_LD_FAMILY_LLD,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "cross_script_sets_entry",
    .project = "test/integration/fixtures/linker/cross",
    .copy = { "main.ld" },
    .when = { .lanes = { "aarch64-gnu" }, .target = "aarch64-freestanding-none" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "aarch64-freestanding-none" } } },
      { .kind = ACTION_VERIFY_ELF_ENTRY, .verify_elf_entry = { target_exe("main", "aarch64-freestanding-none"), 0x400000 } },
    },
  },
  {
    .name = "stub_links_by_install_name",
    .project = "test/integration/fixtures/linker/stub",
    .copy = { "B/*" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "aarch64-macos" } } },
      { .kind = ACTION_VERIFY_MACHO_DYLIB, .verify_macho_dylib = { target_exe("A", "aarch64-macos-apple"), "@rpath/libB.dylib" } },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = target_staged_lib("B", "aarch64-macos-apple") },
    },
  },
  {
    .name = "shared_file_links_stub_and_is_staged",
    .project = "test/integration/fixtures/linker/shared",
    .copy = { "B/*" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "aarch64-macos" } } },
      { .kind = ACTION_VERIFY_MACHO_DYLIB, .verify_macho_dylib = { target_exe("A", "aarch64-macos-apple"), "@rpath/libB.dylib" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = target_staged_lib("B", "aarch64-macos-apple") },
    },
  },
  {
    .name = "mingw_gcc_honors_script",
    .project = "test/integration/fixtures/linker/mingw",
    .copy = { "main.ld" },
    .when = { .target = "x86_64-windows-gnu", .linker = SPN_LD_FAMILY_GNU },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-gnu" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = target_exe("main", "x86_64-windows-gnu") },
    },
  },
  {
    .name = "zig_msvc_exe_links_shared_runtime",
    .project = "test/integration/fixtures/linker/runtime_exe",
    .when = { .lanes = { "zig" }, .target = "x86_64-windows-msvc" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-msvc" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = target_exe("A", "x86_64-windows-msvc") },
    },
  },
  {
    .name = "zig_msvc_shared_lib_refuses_static_runtime",
    .project = "test/integration/fixtures/linker/runtime_lib",
    .copy = { "a.c" },
    .when = { .lanes = { "zig" }, .target = "x86_64-windows-msvc" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-msvc" }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "feature", .value = "static_runtime" } },
    },
  },
  {
    .name = "zig_msvc_shared_lib_links_shared_runtime",
    .project = "test/integration/fixtures/linker/runtime_lib",
    .copy = { "a.c" },
    .when = { .lanes = { "zig" }, .target = "x86_64-windows-msvc" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "-p", "shared", "--target", "x86_64-windows-msvc" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = { PATH_SHARED_LIB, .pkg = "A", .name = "A", .profile = "shared", .triple = "x86_64-windows-msvc" } },
    },
  },
  {
    .name = "zig_msvc_shared_lib_embeds_public_static_dep",
    .project = "test/integration/fixtures/linker/whole_archive",
    .copy = { "a.c", "B/*" },
    .when = { .lanes = { "zig" }, .target = "x86_64-windows-msvc" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-msvc" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = { PATH_SHARED_LIB, .pkg = "A", .name = "A", .triple = "x86_64-windows-msvc" } },
    },
  },
  {
    .name = "zig_msvc_windows_subsystem_enters_main",
    .project = "test/integration/fixtures/linker/subsystem",
    .when = { .lanes = { "zig" }, .target = "x86_64-windows-msvc" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-msvc" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = target_exe("A", "x86_64-windows-msvc") },
    },
  },
};

sp_test_each(linker, cases, test_t, tests) {
  return run_test(t, *it);
}
