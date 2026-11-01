#include "harness.h"

static const test_t tests [] = {
  {
    .name = "applies_to_dep_source",
    .project = "test/integration/fixtures/patch/basic",
    .copy = { "vendor/spum/spn.toml", "patches/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_SYNC_PATCH } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "unused_entry_fails",
    .project = "test/integration/fixtures/patch/unused",
    .copy = { "patches/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "patch_unused" } },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "missing_file_fails",
    .project = "test/integration/fixtures/patch/missing",
    .copy = { "vendor/spum/spn.toml" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "patch_file_missing" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "subject", .value = "core/spum" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "subject", .value = "core/nosuch" } },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = exe("main") },
    },
  },
};

sp_test_each(patch, cases, test_t, tests) {
  return run_test(t, *it);
}

static const rebuild_test_t rebuilds [] = {
  {
    .name = "edit_rebuilds_with_new_content",
    .project = "test/integration/fixtures/patch/edit",
    .copy = { "vendor/spum/spn.toml", "patches/*" },
    .first = {
      .args = { "build" },
      .expect.bin = { .name = "main", .rc = 2 },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "patches/spum.edit.patch", .to = "patches/spum.patch" },
        },
        .command = {
          .args = { "build" },
          .expect.bin = { .name = "main", .rc = 3 },
        },
      },
    },
  },
};

sp_test_each(patch, rebuild, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}
