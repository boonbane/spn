#include "harness.h"

static const test_t tests [] = {
  {
    .name = "inert",
    .project = "test/integration/fixtures/platform/inert",
    .copy = { "main.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "dep_inert",
    .project = "test/integration/fixtures/platform/fingerprint",
    .copy = { "main.c", "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "frameworks",
    .project = "test/integration/fixtures/platform/frameworks",
    .copy = { "main.c" },
    .when.os = SPN_OS_MACOS,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "sdk_lib",
    .project = "test/integration/fixtures/platform/sdk_lib",
    .copy = { "main.c" },
    .when.os = SPN_OS_MACOS,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "macos_min_os",
    .project = "test/integration/fixtures/platform/inert",
    .copy = { "main.c" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "aarch64-macos" } } },
      { .kind = ACTION_VERIFY_MACHO_MIN_OS, .verify_macho_min_os = { target_exe("main", "aarch64-macos-apple"), { 12 } } },
    },
  },
  {
    .name = "win32",
    .project = "test/integration/fixtures/platform/win32",
    .copy = { "main.c" },
    .when.os = SPN_OS_WINDOWS,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "win32_msvc",
    .project = "test/integration/fixtures/platform/win32",
    .copy = { "main.c" },
    .when.target = SPN_TEST_ARCH "-windows-msvc",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", SPN_TEST_ARCH "-windows-msvc" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = target_exe("main", SPN_TEST_ARCH "-windows-msvc") },
    },
  },
  {
    .name = "masm",
    .project = "test/integration/fixtures/platform/masm",
    .copy = { "main.c", "answer.asm" },
    .when.driver = SPN_CC_DRIVER_MSVC,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "atl",
    .project = "test/integration/fixtures/platform/atl",
    .copy = { "main.cpp" },
    .when = { .driver = SPN_CC_DRIVER_MSVC, .cxx = true, .atl = true },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
};

sp_test_each(platform, cases, test_t, tests) {
  return run_test(t, *it);
}
