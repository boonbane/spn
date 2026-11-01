#include "harness.h"

static const test_t tests [] = {
  {
    .name = "build_facts",
    .project = "test/integration/fixtures/defines/simple",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_CC_ARG, .verify_cc_arg = { "-DSPN_BUILD", "/DSPN_BUILD" } },
      { .kind = ACTION_VERIFY_CC_ARG, .verify_cc_arg = { "-DGATE_DEBUG=1", "/DGATE_DEBUG=1" } },
      { .kind = ACTION_VERIFY_NO_CC_ARG, .verify_cc_arg = { "-DGATE_RELEASE=1", "/DGATE_RELEASE=1" } },
    },
  },
  {
    .name = "script_facts",
    .project = "test/integration/fixtures/defines/script",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
    },
  },
  {
    .name = "windows_target",
    .project = "test/integration/fixtures/defines/simple",
    .when.target = "x86_64-windows-gnu",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-gnu" } } },
      { .kind = ACTION_VERIFY_CC_ARG, .verify_cc_arg = { "-DSPN_BUILD_OS_WINDOWS" } },
    },
  },
  {
    .name = "windows_target_msvc",
    .project = "test/integration/fixtures/defines/simple",
    .when.target = "x86_64-windows-msvc",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-windows-msvc" } } },
      { .kind = ACTION_VERIFY_CC_ARG, .verify_cc_arg = { "/DSPN_BUILD_OS_WINDOWS", "-DSPN_BUILD_OS_WINDOWS" } },
    },
  },
};

sp_test_each(defines, cases, test_t, tests) {
  return run_test(t, *it);
}
