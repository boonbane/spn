#include "harness.h"

static const test_t tests [] = {
  {
    .name = "sanitize_clear",
    .project = "test/integration/fixtures/profile/clear",
    .when.sanitize = SPN_SANITIZER_ADDRESS,
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_CC_ARG, .verify_cc_arg = { "-fsanitize=address", "/fsanitize=address" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "-p", "clean" } } },
      { .kind = ACTION_VERIFY_NO_CC_ARG, .verify_cc_arg = { "-fsanitize=address", "/fsanitize=address" } },
    },
  },
  {
    .name = "default_is_musl_static",
    .project = "test/integration/fixtures/profile/override",
    .when = { .os = SPN_OS_LINUX, .driver = SPN_CC_DRIVER_ZIG },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "target", .value = SPN_TEST_ARCH "-linux-musl" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "toolchain", .lane = true } },
    },
  },
  {
    .name = "config_shared_demand_defaults_to_gnu",
    .project = "test/integration/fixtures/consume/multi_kind/shared",
    .copy = { "packages/*" },
    .when = { .host = SPN_OS_LINUX, .target = SPN_TEST_ARCH "-linux-gnu" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "target", .value = SPN_TEST_ARCH "-linux-gnu" } },
    },
  },
  {
    .name = "root_shared_lib_demand_defaults_to_gnu",
    .project = "test/integration/fixtures/target/shared_lib",
    .copy = { "spum.c" },
    .when = { .host = SPN_OS_LINUX, .target = SPN_TEST_ARCH "-linux-gnu" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "target", .value = SPN_TEST_ARCH "-linux-gnu" } },
    },
  },
  {
    .name = "static_config_is_not_a_shared_demand",
    .project = "test/integration/fixtures/consume/multi_kind/static",
    .copy = { "packages/*" },
    .when = { .os = SPN_OS_LINUX, .driver = SPN_CC_DRIVER_ZIG },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "target", .value = SPN_TEST_ARCH "-linux-musl" } },
    },
  },
  {
    .name = "target_with_foreign_arch",
    .project = "test/integration/fixtures/profile/override",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", "x86_64-wasi" }, .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_PROFILE_ARCH } },
    },
  },
  {
    .name = "freestanding_exe_has_no_interp",
    .project = "test/integration/fixtures/profile/freestanding",
    .copy = { "a.c" },
    .when.target = SPN_TEST_ARCH "-freestanding-none",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", SPN_TEST_ARCH "-freestanding-none" } } },
      { .kind = ACTION_VERIFY_NO_INTERP, .verify_no_interp = target_exe("main", SPN_TEST_ARCH "-freestanding-none") },
    },
  },
  {
    .name = "freestanding_image_has_no_sanitizer_runtime",
    .project = "test/integration/fixtures/profile/freestanding",
    .copy = { "a.c" },
    .when.target = SPN_TEST_ARCH "-freestanding-none",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", SPN_TEST_ARCH "-freestanding-none" } } },
      { .kind = ACTION_VERIFY_ELF_NO_SYMBOL, .verify_elf_no_symbol = { target_exe("main", SPN_TEST_ARCH "-freestanding-none"), "__ubsan_" } },
    },
  },
  {
    .name = "freestanding_libs_not_pic",
    .project = "test/integration/fixtures/profile/freestanding",
    .copy = { "a.c" },
    .when.target = SPN_TEST_ARCH "-freestanding-none",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--target", SPN_TEST_ARCH "-freestanding-none" } } },
      { .kind = ACTION_VERIFY_NO_CC_ARG, .verify_cc_arg = { "-fPIC" } },
    },
  },
  {
    .name = "nolibc_exe_has_no_interp",
    .project = "test/integration/fixtures/profile/nolibc",
    .copy = { "a.c" },
    .when = { .host = SPN_OS_LINUX, .target = SPN_TEST_ARCH "-linux-none" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--abi", "none" } } },
      { .kind = ACTION_VERIFY_NO_INTERP, .verify_no_interp = target_exe("main", SPN_TEST_ARCH "-linux-none") },
    },
  },
  {
    .name = "nolibc_libs_not_pic",
    .project = "test/integration/fixtures/profile/nolibc",
    .copy = { "a.c" },
    .when = { .host = SPN_OS_LINUX, .target = SPN_TEST_ARCH "-linux-none" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "--abi", "none" } } },
      { .kind = ACTION_VERIFY_NO_CC_ARG, .verify_cc_arg = { "-fPIC" } },
    },
  },
  {
    .name = "coff_libs_not_pic",
    .project = "test/integration/fixtures/platform/fingerprint",
    .copy = { "main.c", "packages/*" },
    .when = { .os = SPN_OS_WINDOWS, .driver = SPN_CC_DRIVER_CLANG },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_NO_CC_ARG, .verify_cc_arg = { "-fPIC" } },
    },
  },
  {
    .name = "flags",
    .project = "test/integration/fixtures/profile/sanitize",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_CC_ARG, .verify_cc_arg = { "-O0", "/Od" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "-m", "release" } } },
      { .kind = ACTION_VERIFY_CC_ARG, .verify_cc_arg = { "-O2", "/O2" } },
      { .kind = ACTION_VERIFY_NO_CC_ARG, .verify_cc_arg = { "-fsanitize=address", "/fsanitize=address" } },
    },
  },
};

sp_test_each(profile, cases, test_t, tests) {
  return run_test(t, *it);
}

static const rebuild_test_t rebuilds [] = {
  {
    .name = "override_rebuild",
    .project = "test/integration/fixtures/profile/override",
    .first = {
      .args = { "build" },
      .expect = {
        .bin = { .name = "main", .rc = 1 },
        .events = { { .event = SPN_EVENT_TARGET_BUILD_PASSED } },
        .cc = { { .args = { "DFAST" }, .absent = true } },
      },
    },
    .rebuilds = {
      {
        .command = {
          .args = { "build", "--opt", "3" },
          .expect = {
            .bin = { .name = "main", .rc = 2 },
            .events = { { .event = SPN_EVENT_TARGET_BUILD_PASSED } },
            .cc = { { .args = { "DFAST" } } },
          },
        },
      },
      {
        .command = {
          .args = { "build" },
          .expect = {
            .bin = { .name = "main", .rc = 1 },
            .events = { { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true } },
            .cc = { { .args = { "DFAST" }, .absent = true } },
          },
        },
      },
    },
  },
};

sp_test_each(profile, rebuild, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}

static const command_test_t commands [] = {
  {
    .name = "cross_target_macos",
    .project = "test/integration/fixtures/profile/override",
    .when.driver = SPN_CC_DRIVER_ZIG,
    .args = { "build", "--target", "aarch64-macos" },
    .expect = {
      .exists = { target_exe("main", "aarch64-macos-apple") },
      .events = {
        { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "target", .value = "aarch64-macos-apple" },
        { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "toolchain", .lane = true },
      },
    },
  },
  {
    .name = "freestanding_target",
    .project = "test/integration/fixtures/profile/freestanding",
    .copy = { "a.c" },
    .when.target = SPN_TEST_ARCH "-freestanding-none",
    .args = { "build", "--target", SPN_TEST_ARCH "-freestanding-none" },
    .expect = {
      .exists = { target_exe("main", SPN_TEST_ARCH "-freestanding-none") },
      .events = {
        { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "target", .value = SPN_TEST_ARCH "-freestanding-none" },
        { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "toolchain", .lane = true },
      },
    },
  },
  {
    .name = "nolibc_target",
    .project = "test/integration/fixtures/profile/nolibc",
    .copy = { "a.c" },
    .when = { .host = SPN_OS_LINUX, .target = SPN_TEST_ARCH "-linux-none" },
    .args = { "build", "--abi", "none" },
    .expect = {
      .exists = { target_exe("main", SPN_TEST_ARCH "-linux-none") },
      .events = {
        { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "target", .value = SPN_TEST_ARCH "-linux-none" },
        { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "toolchain", .lane = true },
      },
    },
  },
  {
    .name = "gated_reads_host_platform",
    .project = "test/integration/fixtures/profile/gated",
    .when = { .host = SPN_OS_LINUX, .target = SPN_TEST_ARCH "-linux-gnu" },
    .args = { "build" },
    .expect = {
      .exists = { target_exe("main", SPN_TEST_ARCH "-linux-gnu") },
      .events = { { .event = SPN_EVENT_INIT_BUILD_GRAPH, .key = "target", .value = SPN_TEST_ARCH "-linux-gnu" } },
      .cc = { { .args = { "-O3" }, .absent = true } },
    },
  },
};

sp_test_each(profile, commands, command_test_t, commands) {
  return run_command_test(t, *it);
}
