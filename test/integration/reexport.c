#include "harness.h"

static const test_t tests [] = {
  {
    .name = "public_visible",
    .project = "test/integration/fixtures/reexport/public_visible",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "public_shared",
    .project = "test/integration/fixtures/reexport/public_shared",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "private_hidden",
    .project = "test/integration/fixtures/reexport/private_hidden",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_FAILED, .key = "subject.target", .value = "main" } },
    },
  },
  {
    .name = "private_owner",
    .project = "test/integration/fixtures/reexport/private_owner",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "private_subtree",
    .project = "test/integration/fixtures/reexport/private_subtree",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_FAILED, .key = "subject.target", .value = "main" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_TARGET_BUILD_FAILED, .key = "subject.target", .value = "engine" } },
    },
  },
};

sp_test_each(reexport, cases, test_t, tests) {
  return run_test(t, *it);
}
