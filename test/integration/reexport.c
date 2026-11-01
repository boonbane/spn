#include "harness.h"

static const test_t tests [] = {
  {
    .name = "private_hidden",
    .project = "test/integration/fixtures/reexport/private_hidden",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_FAILED } },
    },
  },
  {
    .name = "private_owner",
    .project = "test/integration/fixtures/reexport/private_owner",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_BIN, .bin = { .file = exe("main"), .rc = 0 } },
    },
  },
  {
    .name = "private_subtree",
    .project = "test/integration/fixtures/reexport/private_subtree",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_FAILED } },
    },
  },
};

sp_test_each(reexport, cases, test_t, tests) {
  return run_test(t, *it);
}
