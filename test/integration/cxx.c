#include "harness.h"

static const test_t tests [] = {
  {
    .name = "static_lib",
    .project = "test/integration/fixtures/cxx/static_lib",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "shared_lib",
    .project = "test/integration/fixtures/cxx/shared_lib",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_shared_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "mixed_lib",
    .project = "test/integration/fixtures/cxx/mixed_lib",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "standard",
    .project = "test/integration/fixtures/cxx/standard",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "exceptions_off",
    .project = "test/integration/fixtures/cxx/exceptions_off",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "rtti_off",
    .project = "test/integration/fixtures/cxx/rtti_off",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "static_into_shared",
    .project = "test/integration/fixtures/cxx/static_into_shared",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_shared_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "transitive",
    .project = "test/integration/fixtures/cxx/transitive",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "toolchain",
    .project = "test/integration/fixtures/cxx/toolchain",
    .toolchain = "custom",
    .when.programs = { "cc", "c++", "ar" },
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "bin",
    .project = "test/integration/fixtures/cxx/bin",
    .copy = { "main.cpp" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_STAGE_BARE_RUN, .bare = { .file = exe("main"), .expect = BARE_EXPECT_RUNS } },
    },
  },
  {
    .name = "runtime_shared",
    .project = "test/integration/fixtures/cxx/runtime_shared",
    .copy = { "main.cpp" },
    .when = { .cxx = true, .lanes = { "msvc", "clang-msvc", "clang64", "mingw64", "ucrt64" } },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_STAGE_BARE_RUN, .bare = { .file = exe("main"), .expect = BARE_EXPECT_RUNS } },
    },
  },
  {
    .name = "runtime_shared_cross_mingw",
    .project = "test/integration/fixtures/cxx/runtime_shared",
    .copy = { "main.cpp" },
    .when = { .cxx = true, .target = "x86_64-windows-gnu", .lanes = { "mingw-gnu", "clang-mingw", "clang-mingw-lld" } },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-gnu" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = target_staged_file("libstdc++-6.dll", "x86_64-windows-gnu") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = target_staged_file("libgcc_s_seh-1.dll", "x86_64-windows-gnu") },
    },
  },
  {
    .name = "zig_refuses_runtime_shared",
    .project = "test/integration/fixtures/cxx/runtime_shared",
    .copy = { "main.cpp" },
    .when = { .cxx = true, .driver = SPN_CC_DRIVER_ZIG, .target = SPN_TEST_TRIPLE },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "-p", "A", "--target", SPN_TEST_TRIPLE }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_RUNTIME_UNSUPPORTED },
    },
  },
  {
    .name = "w64devkit_refuses_runtime_shared",
    .project = "test/integration/fixtures/cxx/runtime_shared",
    .copy = { "main.cpp" },
    .when = { .cxx = true, .lanes = { "w64devkit" } },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_RUNTIME_UNSUPPORTED },
    },
  },
  {
    .name = "msvc_sysroot_refuses_runtime_shared",
    .project = "test/integration/fixtures/cxx/runtime_shared",
    .copy = { "main.cpp" },
    .when = { .cxx = true, .lanes = { "clang-xwin", "zig-xwin" }, .target = "x86_64-windows-msvc" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-msvc" }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_RUNTIME_UNSUPPORTED },
    },
  },
  {
    .name = "zig_builds_runtime_static_on_macos",
    .project = "test/integration/fixtures/cxx/runtime_static",
    .copy = { "main.cpp" },
    .when = { .cxx = true, .os = SPN_OS_MACOS, .driver = SPN_CC_DRIVER_ZIG },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "apple_clang_refuses_runtime_static",
    .project = "test/integration/fixtures/cxx/runtime_static",
    .copy = { "main.cpp" },
    .when = { .cxx = true, .os = SPN_OS_MACOS, .driver = SPN_CC_DRIVER_CLANG },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_RUNTIME_UNSUPPORTED },
    },
  },
  {
    .name = "bare_threads",
    .project = "test/integration/fixtures/cxx/bare_threads",
    .copy = { "main.cpp" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_STAGE_BARE_RUN, .bare = { .file = exe("main"), .expect = BARE_EXPECT_RUNS } },
    },
  },
  {
    .name = "script_rejected",
    .project = "test/integration/fixtures/cxx/script_rejected",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_MANIFEST_ISSUES } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_SCRIPT_COMPILE_FAILED } },
    },
  },
};

sp_test_each(cxx, cases, test_t, tests) {
  return run_test(t, *it);
}
