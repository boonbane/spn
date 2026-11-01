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
    .name = "mingw_gcc_honors_script",
    .project = "test/integration/fixtures/linker/mingw",
    .copy = { "main.ld" },
    .when = { .target = "x86_64-windows-gnu", .linker = SPN_LD_FAMILY_GNU },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-gnu" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = target_exe("main", "x86_64-windows-gnu") },
    },
  },
};

sp_test_each(linker, cases, test_t, tests) {
  return run_test(t, *it);
}
