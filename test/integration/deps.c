#include "harness.h"

static const test_t tests [] = {
  {
    .name = "file_basic",
    .project = "test/integration/fixtures/deps/file/basic",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "--force" } } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "spum", .version = "1.0.0" } },
    },
  },
  {
    .name = "file_invalid_manifest",
    .project = "test/integration/fixtures/deps/file/invalid_manifest",
    .copy = { "vendor/spum/spn.toml" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_MANIFEST_ISSUES } },
    },
  },
  {
    .name = "file_name_mismatch",
    .project = "test/integration/fixtures/deps/file/name_mismatch",
    .copy = { "vendor/spum/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "requested", .value = "spum" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "pkg_mismatch" } },
    },
  },
  {
    .name = "file_missing_manifest",
    .project = "test/integration/fixtures/deps/file/missing_manifest",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "no_manifest" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "pkg_unknown" } },
    },
  },
  {
    .name = "file_remote_source",
    .project = "test/integration/fixtures/deps/file/remote_source",
    .copy = { "vendor/spum/spn.toml" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "file_two_paths_one_name",
    .project = "test/integration/fixtures/deps/file/two_paths",
    .copy = { "P/*", "Q/*", "B/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_PKG_CONFLICT_PATH } },
    },
  },
  {
    .name = "file_one_path_two_spellings",
    .project = "test/integration/fixtures/deps/file/two_spellings",
    .copy = { "P/*", "B/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
    },
  },
  {
    .name = "index_name_mismatch",
    .project = "test/integration/fixtures/deps/index/name_mismatch",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "requested", .value = "spum" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "pkg_mismatch" } },
    },
  },
  {
    .name = "index_pinned_commit",
    .project = "test/integration/fixtures/deps/index/pinned_commit",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("index_package_pinned_commit") },
    },
  },
  {
    .name = "index_without_source",
    .project = "test/integration/fixtures/deps/index/without_source",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "index_binary_static",
    .project = "test/integration/fixtures/deps/index/binary_static",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "index_binary_shared",
    .project = "test/integration/fixtures/deps/index/binary_shared",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "index_split_recipe",
    .project = "test/integration/fixtures/deps/index/split_recipe",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked = { .name = "spum", .version = "1.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "index_patched",
    .project = "test/integration/fixtures/deps/index/patched",
    .copy = { "patches/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "index_patched_name_mismatch",
    .project = "test/integration/fixtures/deps/index/patched_name_mismatch",
    .copy = { "patches/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "requested", .value = "spum" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "pkg_mismatch" } },
    },
  },
  {
    .name = "index_fetch_fails",
    .project = "test/integration/fixtures/deps/index/fetch_fails",
    .actions = {
      { .kind = ACTION_REMOVE_DIR, .rm = { .dir = "remote/spum" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_SYNC_FAILED, .key = "name", .value = "spum" } },
    },
  },
  {
    .name = "index_invalid_manifest",
    .project = "test/integration/fixtures/deps/index/invalid_manifest",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "subject", .value = "spum" } },
    },
  },
};

sp_test_each(deps, cases, test_t, tests) {
  return run_test(t, *it);
}

static const rebuild_test_t rebuilds [] = {
  {
    .name = "file_editable",
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
            .locked = { { .name = "spum", .version = "1.0.0" } },
          },
        },
      },
    },
  },
  {
    .name = "index_relocks_after_clean",
    .project = "test/integration/fixtures/deps/index/basic",
    .first = {
      .args = { "build" },
      .expect.locked = { { .name = "spum", .version = "1.0.0" } },
    },
    .rebuilds = {
      {
        .change.remove_dirs = { "build" },
        .command = {
          .args = { "build" },
          .expect.locked = { { .name = "spum", .version = "1.0.0" } },
        },
      },
    },
  },
};

sp_test_each(deps, rebuild, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}

static const command_test_t tools [] = {
  {
    .name = "unselected",
    .project = "test/integration/fixtures/deps/tool/index",
    .args = { "build" },
    .expect.events = { { .event = SPN_EVENT_RESOLVE_PACKAGE, .key = "name", .value = "A" } },
  },
  {
    .name = "selected",
    .project = "test/integration/fixtures/deps/tool/index",
    .copy = { "t.c" },
    .args = { "build", "--tool" },
    .expect.exists = { exe("T") },
  },
  {
    .name = "source_only",
    .project = "test/integration/fixtures/deps/tool/source_only",
    .copy = { "t.c" },
    .args = { "build", "--tool" },
    .expect.events = { { .event = SPN_EVENT_SYNC_PACKAGE, .key = "name", .value = "A" } },
  },
  {
    .name = "sibling_lib_links_package_deps",
    .project = "test/integration/fixtures/deps/tool/sibling",
    .copy = { "l.c", "t.c" },
    .args = { "build", "--tool" },
    .expect.exists = { exe("T") },
  },
  {
    .name = "sibling_lib_dep_at_another_version",
    .project = "test/integration/fixtures/deps/tool/sibling_conflict",
    .copy = { "l.c", "t.c" },
    .args = { "build", "--tool" },
    .expect = { .rc = 1, .err = SPN_ERR_STATIC_DUPLICATE },
  },
};

sp_test_each(deps, tool, command_test_t, tools) {
  return run_command_test(t, *it);
}
