#include "harness.h"

static const test_t tests [] = {
  {
    .name = "kernel",
    .project = "example/kernel",
    .copy = { "source" },
    .when.target = SPN_TEST_ARCH "-freestanding-none",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_ELF_ENTRY, .verify_elf_entry = { target_exe("main", SPN_TEST_ARCH "-freestanding-none"), 0x100000 } },
      { .kind = ACTION_VERIFY_ELF_NO_SYMBOL, .verify_elf_no_symbol = { target_exe("main", SPN_TEST_ARCH "-freestanding-none"), "__ubsan_" } },
    },
  },
  {
    .name = "kernel_aarch64",
    .project = "example/kernel",
    .copy = { "source" },
    .when.target = "aarch64-freestanding-none",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "aarch64-freestanding-none" } } },
      { .kind = ACTION_VERIFY_ELF_ENTRY, .verify_elf_entry = { target_exe("main", "aarch64-freestanding-none"), 0x40100000 } },
      { .kind = ACTION_VERIFY_ELF_NO_SYMBOL, .verify_elf_no_symbol = { target_exe("main", "aarch64-freestanding-none"), "__ubsan_" } },
    },
  },
  {
    .name = "nolibc_exe_has_no_interp",
    .project = "example/nolibc",
    .when = { .host = SPN_OS_LINUX, .target = SPN_TEST_ARCH "-linux-none" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_NO_INTERP, .verify_no_interp = target_exe("main", SPN_TEST_ARCH "-linux-none") },
    },
  },
};

sp_test_each(example, cases, test_t, tests) {
  return run_test(t, *it);
}
