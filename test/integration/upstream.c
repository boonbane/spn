#include "harness.h"

static const command_test_t tests [] = {
  {
    .name = "root_lib",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .args = { "build" },
    .expect.exists = { pkg_static_lib("A", "A") },
  },
  {
    .name = "root_example",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .args = { "build" },
    .expect.exists = { example_exe("E") },
  },
  {
    .name = "recipe_tree_source",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .args = { "build" },
    .expect.exists = { example_exe("R") },
  },
  {
    .name = "recipe_tree_glob",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .args = { "build" },
    .expect.exists = { example_exe("G") },
  },
  {
    .name = "recipe_tree_headers",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .args = { "build" },
    .expect.exists = { pkg_store_file("A", "include/a.h"), pkg_store_file("A", "include/cfg.h") },
  },
  {
    .name = "publish_manifest_tree",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .args = { "build" },
    .expect.exists = { pkg_store_file("A", "include/pub.h") },
  },
  {
    .name = "object_names_are_injective",
    .project = "test/integration/fixtures/upstream/root",
    .copy = { "example", "cfg.h", "pub.h" },
    .args = { "build" },
    .expect.exists = { example_exe("X") },
  },
  {
    .name = "file_dep",
    .project = "test/integration/fixtures/upstream/consume",
    .copy = { "vendor/A/spn.toml" },
    .args = { "build" },
    .expect.exists = { exe("main") },
  },
};

sp_test_each(upstream, cases, command_test_t, tests) {
  return run_command_test(t, *it);
}
