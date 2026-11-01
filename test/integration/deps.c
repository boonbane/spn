#include "harness.h"

static const test_t file_tests [] = {
  {
    .name = "basic",
    .project = "test/integration/fixtures/deps/file/basic",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "--force" } } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "core/spum", .version = "1.0.0" } },
    },
  },
  {
    .name = "invalid_manifest",
    .project = "test/integration/fixtures/deps/file/invalid_manifest",
    .copy = { "vendor/spum/spn.toml" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_MANIFEST_ISSUES } },
    },
  },
  {
    .name = "name_mismatch",
    .project = "test/integration/fixtures/deps/file/name_mismatch",
    .copy = { "vendor/spum/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "requested", .value = "core/spum" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "pkg_mismatch" } },
    },
  },
  {
    .name = "missing_manifest",
    .project = "test/integration/fixtures/deps/file/missing_manifest",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "no_manifest" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "pkg_unknown" } },
    },
  },
  {
    .name = "remote_source",
    .project = "test/integration/fixtures/deps/file/remote_source",
    .copy = { "vendor/spum/spn.toml" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "core/spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
};

sp_test_each(deps_file, cases, test_t, file_tests) {
  return run_test(t, *it);
}

static const rebuild_test_t file_rebuilds [] = {
  {
    .name = "editable",
    .project = "test/integration/fixtures/deps/file/editable",
    .copy = { "packages/*", "main.kram.c" },
    .first = {
      .args = { "build" },
      .expect.exists = { exe("editable_package") },
    },
    .rebuilds = {
      {
        .change = {
          .remove_files = { lit("packages/spum/spum.h") },
          .moves = {
            { .from = "packages/spum/kram.h", .to = "packages/spum/spum.h" },
            { .from = "main.kram.c", .to = "main.c" },
          },
          .remove_dirs = { "build" },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { exe("editable_package") },
            .locked = { { .name = "core/spum", .version = "1.0.0" } },
          },
        },
      },
    },
  },
};

sp_test_each(deps_file, rebuild, rebuild_test_t, file_rebuilds) {
  return run_rebuild_test(t, *it);
}

static const rebuild_test_t index_rebuilds [] = {
  {
    .name = "basic",
    .project = "test/integration/fixtures/deps/index/basic",
    .first = {
      .args = { "build" },
      .expect = {
        .locked = { { .name = "core/spum", .version = "1.0.0" } },
      },
    },
    .rebuilds = {
      {
        .change.remove_dirs = { "build" },
        .command = {
          .args = { "build" },
          .expect = {
            .locked = { { .name = "core/spum", .version = "1.0.0" } },
          },
        },
      },
    },
  },
};

sp_test_each(deps_index, rebuild, rebuild_test_t, index_rebuilds) {
  return run_rebuild_test(t, *it);
}

static const test_t index_tests [] = {
  {
    .name = "name_mismatch",
    .project = "test/integration/fixtures/deps/index/name_mismatch",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "requested", .value = "core/spum" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "pkg_mismatch" } },
    },
  },
  {
    .name = "pinned_commit",
    .project = "test/integration/fixtures/deps/index/pinned_commit",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("index_package_pinned_commit") },
    },
  },
  {
    .name = "without_source",
    .project = "test/integration/fixtures/deps/index/without_source",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "core/spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "binary_static",
    .project = "test/integration/fixtures/deps/index/binary_static",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "core/spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "binary_shared",
    .project = "test/integration/fixtures/deps/index/binary_shared",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "core/spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "split_recipe",
    .project = "test/integration/fixtures/deps/index/split_recipe",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "core/spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "patched",
    .project = "test/integration/fixtures/deps/index/patched",
    .copy = { "patches/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "fetch_fails",
    .project = "test/integration/fixtures/deps/index/fetch_fails",
    .actions = {
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = "remote/spum" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_SYNC_FAILED, .key = "name", .value = "core/spum" } },
    },
  },
  {
    .name = "invalid_manifest",
    .project = "test/integration/fixtures/deps/index/invalid_manifest",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "subject", .value = "core/spum" } },
    },
  },
};

sp_test_each(deps_index, cases, test_t, index_tests) {
  return run_test(t, *it);
}
