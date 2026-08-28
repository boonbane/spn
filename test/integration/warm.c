#include "harness.h"

sp_test(warm, cold_stamps_build_and_report) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/basic",
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_WARM_FAILED } },
    },
  });
}

sp_test(warm, warm_stamps_are_silent) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/basic",
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "--force" } } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
    },
  });
}

sp_test(warm, evicted_stamps_rewarm) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/basic",
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "--force" } } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
    },
  });
}
