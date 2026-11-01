#include "harness.h"

static const test_t tests [] = {
  {
    .name = "root_lib",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("A", "A") },
    },
  },
  {
    .name = "root_example",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = example_exe("E") },
    },
  },
  {
    .name = "recipe_tree_source",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = example_exe("R") },
    },
  },
  {
    .name = "recipe_tree_glob",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = example_exe("G") },
    },
  },
  {
    .name = "recipe_tree_headers",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("A", "include/a.h") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("A", "include/cfg.h") },
    },
  },
  {
    .name = "publish_manifest_tree",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("A", "include/pub.h") },
    },
  },
  {
    .name = "object_names_are_injective",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = example_exe("X") },
    },
  },
  {
    .name = "file_dep",
    .project = "test/integration/fixtures/upstream/consume",
    .copy = { "vendor/A/spn.toml" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
};

sp_test_each(upstream, cases, test_t, tests) {
  return run_test(t, *it);
}
