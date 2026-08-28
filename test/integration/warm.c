#include "harness.h"

sp_test_suite(warm, .serial = true);

sp_test(warm, rebuild_is_silent) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/basic",
    .copy = { "b.c" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
    },
  });
}

sp_test(warm, generation_rewarms) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/basic",
    .copy = { "b.c" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
    },
  });
}

sp_test(warm, shared_stub_warms_once) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/basic",
    .copy = { "b.c" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_BUILD_PASSED, .key = "misses", .value = "1" } },
    },
  });
}
