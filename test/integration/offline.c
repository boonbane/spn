#include "harness.h"

sp_test(offline, store_only) {
  return run_test(t, (test_t) {
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
  });
}

sp_test(offline, store_only_unlocked) {
  return run_rebuild_test(t, (rebuild_test_t) {
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
  });
}

sp_test(offline, relocated_checkout) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/offline/relocated_checkout",
    .copy = { "one/*", "two/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "one" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "main" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "two" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "main" } },
    },
  });
}

sp_test(offline, shared_store) {
  return run_test(t, (test_t) {
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
  });
}
