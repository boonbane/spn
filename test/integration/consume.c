#include "harness.h"

sp_test(consume, static_lib) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/static",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  });
}

sp_test(consume, shared_lib) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/shared",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_shared_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  });
}

sp_test(consume, shared_lib_static_profile) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/shared",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "static" }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_TARGET_LINKAGE } },
    },
  });
}

sp_test(consume, source_lib) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/source",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  });
}

sp_test(consume, system_dep) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/system_dep",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "static" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_profile_static_lib("static", "spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = profile_exe("static", "main") },
    },
  });
}

sp_test(consume, transitive) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/transitive",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spam", "spam") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  });
}

sp_test(consume, explicit_root_with_package_dep) {
  return run_test(t, (test_t) {
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
  });
}

sp_test(consume, dependency_package_is_not_a_root_target) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/root_only",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .args = { "dependency" }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_TARGET_SELECTION } },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = pkg_static_lib("dependency", "dependency") },
    },
  });
}

sp_test(consume, multi_kind_default) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/multi_kind/default",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  });
}

sp_test(consume, multi_kind_static) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/multi_kind/static",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  });
}

sp_test(consume, multi_kind_shared) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/multi_kind/shared",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_shared_lib("spum", "spum") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  });
}

sp_test(consume, multi_kind_source) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/multi_kind/source",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build" } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = exe("main") },
    },
  });
}

sp_test(consume, kind_not_supported) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/consume/kind_not_supported",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_TARGET_LINKAGE } },
    },
  });
}
