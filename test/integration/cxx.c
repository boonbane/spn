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
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
    },
  },
  {
    .name = "shared_lib",
    .project = "test/integration/fixtures/cxx/shared_lib",
    .when.cxx = true,
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_shared_lib("spum", "spum") },
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
    },
  },
  {
    .name = "mixed_lib",
    .project = "test/integration/fixtures/cxx/mixed_lib",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
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
    .when.cxx = true,
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_shared_lib("spum", "spum") },
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
    },
  },
  {
    .name = "transitive",
    .project = "test/integration/fixtures/cxx/transitive",
    .copy = { "packages/*" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
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
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
    },
  },
  {
    .name = "bin",
    .project = "test/integration/fixtures/cxx/bin",
    .copy = { "main.cpp" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
      { .kind = ACTION_STAGE_BARE_RUN, .bare = { .file = exe("main"), .expect = BARE_EXPECT_RUNS } },
    },
  },
  {
    .name = "runtime_shared",
    .project = "test/integration/fixtures/cxx/runtime_shared",
    .copy = { "main.cpp" },
    .when = { .cxx = true, .lanes = { "clang64", "mingw64", "ucrt64", "msvc" } },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_STAGE_BARE_RUN, .bare = { .file = exe("main"), .expect = BARE_EXPECT_NOT_LOADABLE } },
    },
  },
  {
    .name = "bare_threads",
    .project = "test/integration/fixtures/cxx/bare_threads",
    .copy = { "main.cpp" },
    .when.cxx = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
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
  {
    .name = "toolchain_missing",
    .project = "test/integration/fixtures/cxx/toolchain_missing",
    .toolchain = "conly",
    .when.programs = { "cc", "ar" },
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_FAILED } },
    },
  },
};

sp_test_each(cxx, cases, test_t, tests) {
  return run_test(t, *it);
}
