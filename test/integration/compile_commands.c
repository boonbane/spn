#include "harness.h"

static const command_test_t commands [] = {
  {
    .name = "written_on_build",
    .project = "test/integration/fixtures/compile_commands/simple",
    .args = { "build" },
    .expect.files = {
      { lit("compile_commands.json"), .json = true },
    },
  },
  {
    .name = "module_compiles_listed_with_dep_includes",
    .project = "test/integration/fixtures/script/build_dep_closure",
    .args = { "build" },
    .expect.cc = {
      { .file = lit("tools/configure.c") },
      { .file = lit("tools/build.c"), .include = ".st/pkg/alpha/" },
      { .include = ".st/pkg/beta/" },
    },
  },
  {
    .name = "zig_search_is_explicit",
    .project = "test/integration/fixtures/compile_commands/simple",
    .when = { .driver = SPN_CC_DRIVER_ZIG },
    .args = { "build" },
    .expect.cc = {
      { .args = { "-nostdinc" } },
    },
  },
  {
    .name = "gcc_search_is_explicit",
    .project = "test/integration/fixtures/compile_commands/simple",
    .when = { .driver = SPN_CC_DRIVER_GCC },
    .args = { "build" },
    .expect.cc = {
      { .args = { "-nostdlibinc" } },
    },
  },
  {
    .name = "cross_gcc_search_is_explicit",
    .project = "test/integration/fixtures/compile_commands/simple",
    .when = { .driver = SPN_CC_DRIVER_GCC, .target = "aarch64-linux-gnu" },
    .args = { "build", "--target", "aarch64-linux-gnu" },
    .expect.cc = {
      { .args = { "-nostdlibinc" } },
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
      { .kind = ACTION_CREATE_FILE, .create = { .file = "main.c", .from = "main.bad.c" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("compile_commands.json") },
    },
  },
};

sp_test_each(compile_commands, failure, test_t, tests) {
  return run_test(t, *it);
}
