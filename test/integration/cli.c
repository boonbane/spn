#include "harness.h"

static const test_t tests [] = {
  {
    .name = "missing_required_package_version",
    .project = "test/integration/fixtures/cli/missing_required_package_version",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
    },
  },
  {
    .name = "missing_required_package_name",
    .project = "test/integration/fixtures/cli/missing_required_package_name",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
    },
  },
  {
    .name = "wrong_type_required_package_version",
    .project = "test/integration/fixtures/cli/wrong_type_required_package_version",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .rc = 1 } },
    },
  },
  {
    .name = "init",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "init" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("spn.toml") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("main.c") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit(".gitignore") },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "init", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_INIT_EXISTS } },
    },
  },
  {
    .name = "init_existing",
    .actions = {
      { .kind = ACTION_CREATE_FILE, .create = { .file = lit(".gitignore"), .content = "A" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "init", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_INIT_EXISTS } },
    },
  },
  {
    .name = "init_path",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "init", .args = { "sub" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("sub/main.c") },
    },
  },
  {
    .name = "init_bare",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "init", .args = { "sub", "--bare" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("sub/spn.toml") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = lit("sub/main.c") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = lit("sub/.gitignore") },
    },
  },
  {
    .name = "add",
    .project = "test/integration/fixtures/cli/add",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "add", .args = { "spum" } } },
      { .kind = ACTION_VERIFY_FILE_CONTAINS, .verify_file_contains = { .file = "spn.toml", .needle = "spum = \"1.1.0\"" } },
      { .kind = ACTION_VERIFY_FILE_CONTAINS, .verify_file_contains = { .file = "spn.toml", .needle = "# keep" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "add", .args = { "spum@1.0.0" } } },
      { .kind = ACTION_VERIFY_FILE_CONTAINS, .verify_file_contains = { .file = "spn.toml", .needle = "spum = \"1.0.0\"" } },
      { .kind = ACTION_VERIFY_FILE_NOT_CONTAINS, .verify_file_contains = { .file = "spn.toml", .needle = "1.1.0" } },
    },
  },
  {
    .name = "add_test_dep",
    .project = "test/integration/fixtures/cli/add",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "add", .args = { "spum", "--test" } } },
      { .kind = ACTION_VERIFY_FILE_CONTAINS, .verify_file_contains = { .file = "spn.toml", .needle = "[deps.test]" } },
    },
  },
  {
    .name = "add_unknown_package",
    .project = "test/integration/fixtures/cli/add",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "add", .args = { "kram" }, .rc = 1 } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "add", .args = { "spum@9.0.0" }, .rc = 1 } },
    },
  },
  {
    .name = "clean",
    .project = "test/integration/fixtures/cli/add",
    .actions = {
      { .kind = ACTION_CREATE_FILE, .create = { .file = lit("build/debug/marker"), .content = "x" } },
      { .kind = ACTION_CREATE_FILE, .create = { .file = lit("build/release/marker"), .content = "x" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = lit("build/debug") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("build/release") },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean" } },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = lit("build") },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "-p", "../escape" }, .rc = 1 } },
    },
  },
  {
    .name = "index_path",
    .project = "test/integration/fixtures/deps/index/basic",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "index", .args = { "path" } } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "index", .args = { "path", "nope" }, .rc = 1 } },
    },
  },
  {
    .name = "index_sync",
    .project = "test/integration/fixtures/deps/index/basic",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "index", .args = { "sync" } } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "index", .args = { "sync", "nope" }, .rc = 1 } },
    },
  },
  {
    .name = "publish_dry_run",
    .project = "test/integration/fixtures/deps/index/basic",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "publish", .args = { "--dry", "--source-url", "https://example.com/x.git", "--source-rev", "abc123" } } },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = lit(".home/storage/spn/packages/core/index_package.jsonl") },
    },
  },
  {
    .name = "workspace_index",
    .project = "test/integration/fixtures/cli/workspace_index",
    .copy = { "index/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_RUN_BIN, .bin = { .file = exe("main"), .rc = 3 } },
    },
  },
  {
    .name = "workspace_dir_index",
    .project = "test/integration/fixtures/cli/workspace_dir_index",
    .copy = { "index/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "user_dir_index",
    .project = "test/integration/fixtures/cli/user_dir_index",
    .config = "config.toml",
    .actions = {
      {
        .kind = ACTION_CREATE_FILE,
        .create = {
          .file = lit(".home/config/spn/index/A/spn.toml"),
          .content = "[package]\nname = \"A\"\nversion = \"1.0.0\"\n\n[[lib]]\nname = \"A\"\nkinds = [\"source\"]\npublish.include = [{ from = \"source/A.h\" }]\n",
        },
      },
      {
        .kind = ACTION_CREATE_FILE,
        .create = {
          .file = lit(".home/config/spn/index/A/A.h"),
          .content = "#pragma once\n\nstatic inline int A(void) {\n  return 0;\n}\n",
        },
      },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
};

sp_test_each(cli, cases, test_t, tests) {
  return run_test(t, *it);
}
