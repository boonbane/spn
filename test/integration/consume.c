#include "harness.h"

static const test_t tests [] = {
  {
    .name = "static_lib",
    .project = "test/integration/fixtures/consume/static",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "shared_lib",
    .project = "test/integration/fixtures/consume/shared",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_shared_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "source_lib",
    .project = "test/integration/fixtures/consume/source",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "system_dep",
    .project = "test/integration/fixtures/consume/system_dep",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "static" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_profile_static_lib("static", "spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = profile_exe("static", "main") },
    },
  },
  {
    .name = "transitive",
    .project = "test/integration/fixtures/consume/transitive",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spam", "spam") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "explicit_root_with_package_dep",
    .project = "test/integration/fixtures/consume/root_only",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "main" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("dependency", "dependency") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_file("dependency", "bin/dependency-bin") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_store_file("dependency", "bin/dependency-script") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = test_exe("dependency-test") },
    },
  },
  {
    .name = "dependency_package_is_not_a_root_target",
    .project = "test/integration/fixtures/consume/root_only",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "dependency" }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_TARGET_SELECTION } },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_static_lib("dependency", "dependency") },
    },
  },
  {
    .name = "multi_kind_default",
    .project = "test/integration/fixtures/consume/multi_kind/default",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "multi_kind_static",
    .project = "test/integration/fixtures/consume/multi_kind/static",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "multi_kind_shared",
    .project = "test/integration/fixtures/consume/multi_kind/shared",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_shared_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
  {
    .name = "multi_kind_source",
    .project = "test/integration/fixtures/consume/multi_kind/source",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  },
};

sp_test_each(consume, cases, test_t, tests) {
  return run_test(t, *it);
}
