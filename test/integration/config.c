#include "harness.h"

static const test_t tests [] = {
  {
    .name = "toolchain_is_selectable",
    .project = "test/integration/fixtures/config/toolchain",
    .config = "config.toml",
    .toolchain = "T",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_TOOLCHAIN_MISSING },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "program", .value = "/Q" } },
    },
  },
  {
    .name = "toolchain_overrides_builtin",
    .project = "test/integration/fixtures/config/builtin",
    .config = "config.toml",
    .toolchain = "gcc",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_TOOLCHAIN_MISSING },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "program", .value = "/Q" } },
    },
  },
  {
    .name = "project_toolchain_wins",
    .project = "test/integration/fixtures/config/override",
    .config = "config.toml",
    .toolchain = "T",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result.err = SPN_ERR_TOOLCHAIN_MISSING },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_ERR, .key = "program", .value = "/R" } },
    },
  },
  {
    .name = "storage_dir_not_a_dir",
    .actions = {
      { .kind = ACTION_CREATE_FILE, .create = { .file = "S", .content = "A" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "init", .env = { "SPN_STORAGE_DIR=S" }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_FS_CREATE_DIR } },
    },
  },
};

sp_test_each(config, cases, test_t, tests) {
  return run_test(t, *it);
}
