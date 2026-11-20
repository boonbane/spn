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
      { .kind = ACTION_VERIFY_STORE, .verify_store = { .pkg = "spum", .count = 1 } },
    },
  },
  {
    .name = "index_not_needed",
    .project = "test/integration/fixtures/freshness/bin",
    .actions = {
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = ".st/spn/packages" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { "test" } },
    },
  },
  {
    .name = "index_unreachable",
    .project = "test/integration/fixtures/deps/index/binary_static",
    .actions = {
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = ".st/index" } },
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = ".st/spn/packages" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_INDEX_SYNC },
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
            .locked = { { .name = "spum", .version = "1.0.0" } },
          },
        },
      },
    },
  },
  {
    .name = "stale_index_unreachable",
    .project = "test/integration/fixtures/deps/index/binary_static",
    .first.args = { "build" },
    .rebuilds = {
      {
        .change = {
          .age_files = { index_file("spn-tip") },
          .remove_dirs = { ".st/spn/packages" },
        },
        .command = {
          .args = { "build" },
          .expect.events = { { .event = SPN_EVENT_SYNC_STALE } },
        },
      },
    },
  },
  {
    .name = "aged_tip_is_read",
    .project = "test/integration/fixtures/deps/index/binary_static",
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.age_files = { index_file("spn-tip") },
        .command = {
          .args = { "build" },
          .expect.events = { { .event = SPN_EVENT_SYNC, .key = "name", .value = "core" } },
        },
      },
    },
  },
  {
    .name = "failed_refresh_is_not_retried",
    .project = "test/integration/fixtures/deps/index/binary_static",
    .first.args = { "add", "spum" },
    .rebuilds = {
      {
        .change = {
          .age_files = { index_file("spn-tip") },
          .remove_dirs = { ".st/spn/packages" },
        },
        .command = {
          .args = { "add", "spum" },
          .expect.events = {
            { .event = SPN_EVENT_SYNC, .key = "name", .value = "core" },
            { .event = SPN_EVENT_SYNC_STALE },
          },
        },
      },
      {
        .command = {
          .args = { "add", "spum" },
          .expect.events = {
            { .event = SPN_EVENT_SYNC, .absent = true },
            { .event = SPN_EVENT_SYNC_STALE },
          },
        },
      },
    },
  },
};

sp_test_each(offline, rebuild, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}
