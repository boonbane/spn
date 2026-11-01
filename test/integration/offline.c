#include "harness.h"

static const test_t tests [] = {
  {
    .name = "store_only",
    .project = "test/integration/fixtures/deps/index/binary_static",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = "remote/spum" } },
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_SYNC_FAILED } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "relocated_checkout",
    .project = "test/integration/fixtures/offline/relocated_checkout",
    .copy = { "one/*", "two/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "one" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "main" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "two" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "main" } },
    },
  },
  {
    .name = "shared_store",
    .project = "test/integration/fixtures/offline/shared_store",
    .copy = { "second/*" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = "remote/spum" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "second" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = { PATH_EXE, .name = "main", .dir = "second" } },
      { .kind = ACTION_VERIFY_STORE, .verify_store = { .pkg = "core/spum", .count = 1 } },
    },
  },
};

sp_test_each(offline, cases, test_t, tests) {
  return run_test(t, *it);
}

static const rebuild_test_t rebuilds [] = {
  {
    .name = "store_only_unlocked",
    .project = "test/integration/fixtures/deps/index/binary_static",
    .first = {
      .args = { "build" },
      .expect.exists = { exe("main") },
    },
    .rebuilds = {
      {
        .change = {
          .remove_files = { lit("spn.lock") },
          .remove_dirs = { "remote/spum", "build" },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SYNC_FAILED, .absent = true } },
            .exists = { exe("main") },
            .locked = { { .name = "core/spum", .version = "1.0.0" } },
          },
        },
      },
    },
  },
};

sp_test_each(offline, rebuild, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}
