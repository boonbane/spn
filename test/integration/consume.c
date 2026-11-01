#include "harness.h"

static const command_test_t tests [] = {
  {
    .name = "static_lib",
    .project = "test/integration/fixtures/consume/static",
    .copy = { "packages/*" },
    .args = { "build", "-p", "debug" },
    .expect.exists = { pkg_static_lib("spum", "spum"), exe("main") },
  },
  {
    .name = "shared_lib",
    .project = "test/integration/fixtures/consume/shared",
    .copy = { "packages/*" },
    .args = { "build", "-p", "debug" },
    .expect.exists = { pkg_shared_lib("spum", "spum"), exe("main") },
  },
  {
    .name = "source_lib",
    .project = "test/integration/fixtures/consume/source",
    .copy = { "packages/*" },
    .args = { "build", "-p", "debug" },
    .expect.exists = { exe("main") },
  },
  {
    .name = "system_dep",
    .project = "test/integration/fixtures/consume/system_dep",
    .copy = { "packages/*" },
    .args = { "build", "-p", "static" },
    .expect.exists = { pkg_profile_static_lib("static", "spum", "spum"), profile_exe("static", "main") },
  },
  {
    .name = "transitive",
    .project = "test/integration/fixtures/consume/transitive",
    .copy = { "packages/*" },
    .args = { "build", "-p", "debug" },
    .expect.exists = { pkg_static_lib("spum", "spum"), pkg_static_lib("spam", "spam"), exe("main") },
  },
  {
    .name = "explicit_root_with_package_dep",
    .project = "test/integration/fixtures/consume/root_only",
    .copy = { "packages/*" },
    .args = { "build", "main" },
    .expect = {
      .exists = { pkg_static_lib("dependency", "dependency"), exe("main") },
      .missing = { pkg_store_exe("dependency", "dependency-bin"), pkg_store_exe("dependency", "dependency-script"), test_exe("dependency-test") },
    },
  },
  {
    .name = "dependency_package_is_not_a_root_target",
    .project = "test/integration/fixtures/consume/root_only",
    .copy = { "packages/*" },
    .args = { "build", "dependency" },
    .expect = {
      .rc = 1,
      .err = SPN_ERR_TARGET_SELECTION,
      .missing = { pkg_static_lib("dependency", "dependency") },
    },
  },
  {
    .name = "multi_kind_default",
    .project = "test/integration/fixtures/consume/multi_kind/default",
    .copy = { "packages/*" },
    .args = { "build" },
    .expect.exists = { exe("main") },
  },
  {
    .name = "multi_kind_static",
    .project = "test/integration/fixtures/consume/multi_kind/static",
    .copy = { "packages/*" },
    .args = { "build" },
    .expect.exists = { pkg_static_lib("spum", "spum"), exe("main") },
  },
  {
    .name = "multi_kind_shared",
    .project = "test/integration/fixtures/consume/multi_kind/shared",
    .copy = { "packages/*" },
    .args = { "build" },
    .expect.exists = { pkg_shared_lib("spum", "spum"), exe("main") },
  },
  {
    .name = "multi_kind_source",
    .project = "test/integration/fixtures/consume/multi_kind/source",
    .copy = { "packages/*" },
    .args = { "build" },
    .expect.exists = { exe("main") },
  },
};

sp_test_each(consume, cases, command_test_t, tests) {
  return run_command_test(t, *it);
}
