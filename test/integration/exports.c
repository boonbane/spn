#include "harness.h"

static const test_t tests [] = {
  {
    .name = "own_exported",
    .project = "test/integration/fixtures/exports/own_exported",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_BIN, .bin.file = exe("main") },
    },
  },
  {
    .name = "private_hidden",
    .project = "test/integration/fixtures/exports/private_hidden",
    .when.exports = true,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_BIN, .bin.file = exe("main") },
    },
  },
  {
    .name = "public_reexported",
    .project = "test/integration/fixtures/exports/public_reexported",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_BIN, .bin.file = exe("main") },
    },
  },
  {
    .name = "collision_loud",
    .project = "test/integration/fixtures/exports/collision_loud",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_LINK_FAILED } },
    },
  },
  {
    .name = "no_interference",
    .project = "test/integration/fixtures/exports/no_interference",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_BIN, .bin.file = exe("main") },
    },
  },
};

sp_test_each(exports, cases, test_t, tests) {
  return run_test(t, *it);
}
