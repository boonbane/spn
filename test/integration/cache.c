#include "harness.h"

static const test_t tests [] = {
  {
    .name = "build_sweeps",
    .project = "test/integration/fixtures/cache/A",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_SWEEP } },
    },
  },
  {
    .name = "sweep_waits_for_interval",
    .project = "test/integration/fixtures/cache/A",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_SWEEP } },
    },
  },
  {
    .name = "zero_interval_sweeps_every_build",
    .project = "test/integration/fixtures/cache/A",
    .config = "eager.toml",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_SWEEP } },
    },
  },
  {
    .name = "unlimited_never_sweeps",
    .project = "test/integration/fixtures/cache/A",
    .config = "unlimited.toml",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_SWEEP } },
    },
  },
  {
    .name = "clean_keeps_recently_used",
    .project = "test/integration/fixtures/cache/A",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "--cache" } } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_CLEAN, .key = "removed", .value = "0" } },
    },
  },
};

sp_test_each(cache, cases, test_t, tests) {
  return run_test(t, *it);
}
