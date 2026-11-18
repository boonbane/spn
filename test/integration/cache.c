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
  {
    .name = "clean_evicts_past_min_age",
    .project = "test/integration/fixtures/cache/A",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_AGE_CACHE, .age = { .seconds = 4000 } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "--cache" } } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_CLEAN } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_CLEAN, .key = "removed", .value = "0" } },
    },
  },
  {
    .name = "clean_allows_for_touch_interval",
    .project = "test/integration/fixtures/cache/A",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_AGE_CACHE, .age = { .seconds = 3700 } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "--cache" } } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_CLEAN, .key = "removed", .value = "0" } },
    },
  },
  {
    .name = "min_age_flag_overrides_config",
    .project = "test/integration/fixtures/cache/A",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_AGE_CACHE, .age = { .seconds = 1800 } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "--cache", "--min-age", "900" } } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_CLEAN } },
      { .kind = ACTION_VERIFY_NO_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_CLEAN, .key = "removed", .value = "0" } },
    },
  },
  {
    .name = "min_age_flag_allows_for_touch_interval",
    .project = "test/integration/fixtures/cache/A",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_AGE_CACHE, .age = { .seconds = 1000 } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "--cache", "--min-age", "900" } } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_CLEAN, .key = "removed", .value = "0" } },
    },
  },
  {
    .name = "min_age_flag_below_least_saturates",
    .project = "test/integration/fixtures/cache/A",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build" } },
      { .kind = ACTION_AGE_CACHE, .age = { .seconds = 700 } },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "--cache", "--min-age", "1" } } },
      { .kind = ACTION_VERIFY_EVENT, .verify_event = { .event = SPN_EVENT_CACHE_CLEAN, .key = "removed", .value = "0" } },
    },
  },
  {
    .name = "min_age_flag_needs_cache",
    .project = "test/integration/fixtures/cache/A",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "--min-age", "900" }, .rc = 1 } },
    },
  },
};

sp_test_each(cache, cases, test_t, tests) {
  return run_test(t, *it);
}
