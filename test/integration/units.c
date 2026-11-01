#include "harness.h"

static const command_test_t commands [] = {
  {
    .name = "build_dep_conflict",
    .project = "test/integration/fixtures/units/build_dep_conflict",
    .args = { "build" },
    .expect.exists = { exe("main") },
  },
  {
    .name = "build_dep_transitive_conflict",
    .project = "test/integration/fixtures/units/build_dep_transitive_conflict",
    .args = { "build" },
    .expect.exists = { exe("main") },
  },
  {
    .name = "shared_private",
    .project = "test/integration/fixtures/units/shared_private",
    .args = { "build" },
    .expect.exists = { exe("main") },
  },
};

sp_test_each(units, commands, command_test_t, commands) {
  return run_command_test(t, *it);
}

static const test_t tests [] = {
  {
    .name = "shared_conflict",
    .project = "test/integration/fixtures/units/shared_conflict",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "pkg_conflict" } },
    },
  },
  {
    .name = "static_conflict",
    .project = "test/integration/fixtures/units/static_conflict",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "pkg_conflict" } },
    },
  },
  {
    .name = "no_double_build",
    .project = "test/integration/fixtures/units/no_double_build",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_RESOLVE_PACKAGE, .key = "version", .value = "1.5.0" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_RESOLVE_PACKAGE, .key = "version", .value = "2.0.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "no_downgrade",
    .project = "test/integration/fixtures/units/no_downgrade",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_RESOLVE_PACKAGE, .key = "version", .value = "1.9.0" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "build_dep_cycle",
    .project = "test/integration/fixtures/units/build_dep_cycle",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "kind", .value = "unit_cycle" } },
    },
  },
  {
    .name = "build_dep_bootstrap",
    .project = "test/integration/fixtures/units/build_dep_bootstrap",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
};

sp_test_each(units, cases, test_t, tests) {
  return run_test(t, *it);
}
