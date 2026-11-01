#include "harness.h"

static const test_t tests [] = {
  {
    .name = "static_lib",
    .project = "test/integration/fixtures/target/static_lib",
    .copy = { "mylib.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("static_lib", "mylib") },
    },
  },
  {
    .name = "shared_lib",
    .project = "test/integration/fixtures/target/shared_lib",
    .copy = { "spum.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_shared_lib("test", "spum") },
    },
  },
  {
    .name = "shared_source",
    .project = "test/integration/fixtures/target/shared_source",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "multiple_roots",
    .project = "test/integration/fixtures/target/shared_source",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "main", "test" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("test", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = test_exe("test") },
    },
  },
  {
    .name = "same_name",
    .project = "test/integration/fixtures/target/same_name",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("test", "A") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("A") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = test_exe("T") },
    },
  },
  {
    .name = "selection_default",
    .project = "test/integration/fixtures/target/selection",
    .copy = { "spum.c", "script.c", "x.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("test", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = test_exe("test") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = example_exe("x") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_exe("test", "script") },
    },
  },
  {
    .name = "selection_example",
    .project = "test/integration/fixtures/target/selection",
    .copy = { "spum.c", "script.c", "x.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "--example" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = example_exe("x") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_exe("test", "main") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = test_exe("test") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_exe("test", "script") },
    },
  },
  {
    .name = "selection_named_library",
    .project = "test/integration/fixtures/target/selection_libs",
    .copy = { "one.c", "two.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "one" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("selection_libs", "one") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_static_lib("selection_libs", "two") },
    },
  },
  {
    .name = "selection_multiple_kinds",
    .project = "test/integration/fixtures/target/selection",
    .copy = { "spum.c", "script.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "--bin", "--test" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("test", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = test_exe("test") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_exe("test", "script") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = example_exe("x") },
    },
  },
  {
    .name = "selection_name_respects_kind",
    .project = "test/integration/fixtures/target/selection",
    .copy = { "spum.c", "script.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "--lib", "main" }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_TARGET_SELECTION } },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_exe("test", "main") },
    },
  },
  {
    .name = "selection_test_command",
    .project = "test/integration/fixtures/target/selection",
    .copy = { "spum.c", "script.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "test", .args = { "test" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = test_exe("test") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("test", "spum") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_exe("test", "main") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_exe("test", "script") },
    },
  },
  {
    .name = "selection_named_script",
    .project = "test/integration/fixtures/target/selection",
    .copy = { "spum.c", "script.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "script" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("script") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_exe("test", "main") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = test_exe("test") },
    },
  },
  {
    .name = "example",
    .project = "test/integration/fixtures/target/example",
    .copy = { "a.c", "a.h", "example" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("A", "A") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = example_exe("E") },
    },
  },
  {
    .name = "root_include",
    .project = "test/integration/fixtures/target/root_include",
    .copy = { "a.h", "src" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "publish",
    .project = "test/integration/fixtures/target/publish",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("kit", "include/kit.h") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("kit", "include/kit/a.h") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("kit", "include/kit/b.h") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("kit", "include/kit/deep/x/y.h") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("kit", "include/lit/kit.h") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("kit", "include/kit/on.h") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_file("kit", "include/kit/off.h") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("publish") },
    },
  },
  {
    .name = "publish_absent",
    .project = "test/integration/fixtures/target/publish_absent",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_NODE_FAILED } },
    },
  },
  {
    .name = "system_deps",
    .project = "test/integration/fixtures/target/system_deps",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "lib_system_deps",
    .project = "test/integration/fixtures/target/lib_system_deps",
    .copy = { "mathy.c", "direct.c" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("direct") },
    },
  },
  {
    .name = "link_flags",
    .project = "test/integration/fixtures/target/link_flags",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_LINK_FAILED } },
      { .kind = ACTION_VERIFY_NO_CC_ARG, .verify_cc_arg = { "-lmissing", "/DEFAULTLIB:missing" } },
    },
  },
};

sp_test_each(target, cases, test_t, tests) {
  return run_test(t, *it);
}

static const rebuild_test_t rebuilds [] = {
  {
    .name = "publish_prune",
    .project = "test/integration/fixtures/target/publish",
    .copy = { "packages/*" },
    .first = {
      .args = { "build" },
      .expect.exists = { pkg_store_file("kit", "include/lit/kit.h") },
    },
    .rebuilds = {
      {
        .change.moves = { { .from = "packages/kit/spn.fewer.toml", .to = "packages/kit/spn.toml" } },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { pkg_store_file("kit", "include/kit.h") },
            .missing = { pkg_store_file("kit", "include/lit/kit.h") },
          },
        },
      },
    },
  },
};

sp_test_each(target, rebuild, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}

sp_test(target, cross_exe) {
  const c8* triple = test_target_alternate();
  if (!triple) {
    return sp_test_skip(t, "lane has no cross target");
  }
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/profile/override",
    .when.target = triple,
    .args = { "build", "--target", triple },
    .expect.exists = { { PATH_EXE, .name = "main", .triple = triple } },
  });
}
