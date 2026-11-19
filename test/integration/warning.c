#include "harness.h"

static const command_test_t commands [] = {
  {
    .name = "shown",
    .project = "test/integration/fixtures/warning/root",
    .args = { "build" },
    .expect.events = {
      { .event = SPN_EVENT_TARGET_BUILD_WARNED, .key = "subject.target", .value = "A" },
    },
  },
  {
    .name = "counted",
    .project = "test/integration/fixtures/warning/root",
    .args = { "build" },
    .expect.events = {
      { .event = SPN_EVENT_BUILD_PASSED, .key = "warned", .value = "1" },
    },
  },
  {
    .name = "silent",
    .project = "test/integration/fixtures/freshness/bin",
    .args = { "build" },
    .expect.events = {
      { .event = SPN_EVENT_TARGET_BUILD_WARNED, .absent = true },
      { .event = SPN_EVENT_LINK_WARNED, .absent = true },
    },
  },
  {
    .name = "silent_shared",
    .project = "test/integration/fixtures/warning/shared",
    .copy = { "a.c" },
    .args = { "build" },
    .expect.events = {
      { .event = SPN_EVENT_LINK_WARNED, .absent = true },
    },
  },
  {
    .name = "silent_exports",
    .project = "test/integration/fixtures/warning/exports",
    .when.os = SPN_OS_WINDOWS,
    .args = { "build" },
    .expect.events = {
      { .event = SPN_EVENT_LINK_WARNED, .absent = true },
    },
  },
  {
    .name = "silent_masm",
    .project = "test/integration/fixtures/platform/masm",
    .copy = { "answer.asm" },
    .when.driver = SPN_CC_DRIVER_MSVC,
    .args = { "build" },
    .expect.events = {
      { .event = SPN_EVENT_TARGET_BUILD_WARNED, .absent = true },
    },
  },
  {
    .name = "dependency",
    .project = "test/integration/fixtures/warning/dep",
    .copy = { "B/*" },
    .args = { "build" },
    .expect.events = {
      { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "B" },
      { .event = SPN_EVENT_TARGET_BUILD_WARNED, .absent = true },
    },
  },
  {
    .name = "failed",
    .project = "test/integration/fixtures/warning/failed",
    .copy = { "bad.c" },
    .args = { "build" },
    .expect = {
      .rc = 1,
      .events = {
        { .event = SPN_EVENT_TARGET_BUILD_WARNED, .absent = true },
      },
    },
  },
};

static const rebuild_test_t rebuilds [] = {
  {
    .name = "replayed",
    .project = "test/integration/fixtures/warning/root",
    .first = {
      .args = { "build" },
    },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
            { .event = SPN_EVENT_TARGET_BUILD_WARNED, .key = "subject.target", .value = "A" },
          },
        },
      },
    },
  },
};

sp_test_each(warning, commands, command_test_t, commands) {
  return run_command_test(t, *it);
}

sp_test_each(warning, rebuilds, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}
