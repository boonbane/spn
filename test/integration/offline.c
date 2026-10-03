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
          .remove_files = { sp_str_lit("spn.lock") },
          .remove_dirs = { sp_str_lit("remote/spum"), sp_str_lit("build") },
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

sp_test(offline, no_source_cache) {
  return sp_test_skip(t, "a store hit still syncs sources when the checkout is gone; passes once store-only consumption covers resolve");

  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/deps/index/binary_static",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = "remote/spum" } },
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = ".home/storage/cache/source" } },
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
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
      { .kind = ACTION_VERIFY_EXISTS, .exists = in_dir("second", exe("main")) },
      { .kind = ACTION_VERIFY_STORE, .verify_store = { .pkg = "core/spum", .count = 1 } },
    },
  });
}

sp_test(offline, relocated_checkout) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/offline/relocated_checkout",
    .copy = { "one/*", "two/*" },
    .when.deterministic = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "one" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "target", .value = "main" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "two" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_PASSED } },
      { .kind = ACTION_VERIFY_NO_FIXTURE_PATH, .verify_no_fixture_path = {
        .file = in_dir("two", work_file("relocate/object/exe/main/manifest/main.c.o")),
        .dir = "one",
      } },
    },
  });
}
