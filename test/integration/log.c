#include "harness.h"

static const command_test_t tests [] = {
  {
    .name = "hidden_normally",
    .project = "test/integration/fixtures/log/script_log",
    .args = { "build" },
    .human = true,
    .expect.excludes = { "spn-script-probe-log" },
  },
  {
    .name = "shown_on_failure",
    .project = "test/integration/fixtures/log/script_log_fail",
    .args = { "build" },
    .human = true,
    .expect = {
      .rc = 1,
      .contains = { "spn-script-probe-fail" },
    },
  },
};

sp_test_each(user_log, cases, command_test_t, tests) {
  return run_command_test(t, *it);
}
