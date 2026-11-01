#include "harness.h"

sp_test_suite(warm, .serial = true);

static const test_t tests [] = {
  {
    .name = "rebuild_is_silent",
    .project = "test/integration/fixtures/warm/basic",
    .copy = { "b.c" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
    },
  },
  {
    .name = "generation_rewarms",
    .project = "test/integration/fixtures/warm/basic",
    .copy = { "b.c" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
    },
  },
  {
    .name = "shared_stub_warms_once",
    .project = "test/integration/fixtures/warm/basic",
    .copy = { "b.c" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_BUILD_PASSED, .key = "misses", .value = "1" } },
    },
  },
};

sp_test_each(warm, cases, test_t, tests) {
  return run_test(t, *it);
}
