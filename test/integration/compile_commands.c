#include "harness.h"

static const command_test_t commands [] = {
  {
    .name = "written_on_build",
    .project = "test/integration/fixtures/compile_commands/simple",
    .args = { "build" },
    .expect.files = {
      { .file = lit("compile_commands.json"), .json = true },
    },
  },
  {
    .name = "module_flags",
    .project = "test/integration/fixtures/script/build_dep_closure",
    .args = { "build" },
    .expect = {
      .files = {
        { .file = lit("compile_commands.json"), .json = true },
      },
      .cc = {
        { .args = { "wasm32-wasi" } },
        { .args = { "store/core/alpha" } },
        { .args = { "store/core/beta" } },
      },
    },
  },
};

sp_test_each(compile_commands, cases, command_test_t, commands) {
  return run_command_test(t, *it);
}

static const test_t tests [] = {
  {
    .name = "written_when_compile_fails",
    .project = "test/integration/fixtures/compile_commands/simple",
    .actions = {
      { .kind = ACTION_CREATE_FILE, .create = { .file = lit("main.c"), .content = "int main( {" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("compile_commands.json") },
    },
  },
};

sp_test_each(compile_commands, failure, test_t, tests) {
  return run_test(t, *it);
}
