#include "harness.h"

sp_test_suite(warm, .serial = true);

sp_test(warm, cold_stamps_build_and_report) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/basic",
    .copy = { "b.c" },
    .when.driver = SPN_CC_DRIVER_ZIG,
    .actions = {
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
    },
  });
}

sp_test(warm, fake_cold_reports) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/fake",
    .toolchain = "Z",
    .actions = {
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START, .key = "toolchain", .value = "Z" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_LINK_START } },
    },
  });
}

sp_test(warm, fake_warm_is_silent) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/fake",
    .toolchain = "Z",
    .actions = {
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
    },
  });
}

sp_test(warm, fake_evicted_rewarms) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/fake",
    .toolchain = "Z",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_WARM_START } },
    },
  });
}

sp_test(warm, fake_failure_reports) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/warm/fake",
    .toolchain = "F",
    .actions = {
      { .kind = ACTION_CLEAR_WARM },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_WARM_FAILED, .key = "rc", .value = "3" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_LINK_START } },
    },
  });
}
