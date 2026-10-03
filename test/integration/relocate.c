#include "harness.h"

sp_test(relocate, checkout) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/relocate/project",
    .copy = { "A/*", "B/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "A" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "B" } },
      { .kind = ACTION_VERIFY_NO_FIXTURE_PATH, .verify_no_fixture_path = { .file = in_dir("B", exe("main")), .dir = "A" } },
    },
  });
}

sp_test(relocate, compile_commands) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/relocate/project",
    .copy = { "A/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "A" } },
      { .kind = ACTION_MOVE_DIR, .move = { .from = "A", .to = "B" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .cwd = "B" } },
      { .kind = ACTION_VERIFY_NO_FIXTURE_PATH, .verify_no_fixture_path = { .file = sp_str_lit("B/compile_commands.json"), .dir = "A" } },
    },
  });
}

sp_test(relocate, storage) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/relocate/storage",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .env = { "SPN_STORAGE_DIR=S" } } },
      { .kind = ACTION_MOVE_DIR, .move = { .from = "S", .to = "T" } },
      { .kind = ACTION_RUN_CLI, .cli = { "build", .env = { "SPN_STORAGE_DIR=T" } } },
      { .kind = ACTION_VERIFY_NO_FIXTURE_PATH, .verify_no_fixture_path = { .file = exe("main"), .dir = "S" } },
    },
  });
}
