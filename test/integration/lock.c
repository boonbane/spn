#include "harness.h"

static const command_test_t commands [] = {
  {
    .name = "pins_index_deps",
    .project = "test/integration/fixtures/lock/pair",
    .args = { "build" },
    .expect.locked = {
      { .name = "A", .version = "1.0.0" },
      { .name = "B", .version = "1.0.0" },
    },
  },
  {
    .name = "skips_path_deps",
    .project = "test/integration/fixtures/deps/file/basic",
    .copy = { "packages/*" },
    .args = { "build" },
    .expect.files = { { .file = lit("spn.lock"), .content = "version = 1\n" } },
  },
  {
    .name = "replaces_missing_pin",
    .project = "test/integration/fixtures/lock/gone",
    .copy = { "spn.lock" },
    .args = { "build" },
    .expect.locked = { { .name = "A", .version = "1.1.0" } },
  },
  {
    .name = "unpinned_selection_rejected",
    .project = "test/integration/fixtures/lock/unpinned",
    .args = { "build" },
    .expect = { .rc = 1, .err = SPN_ERR_LOCK_UNPINNED },
  },
  {
    .name = "pins_closed_gates",
    .project = "test/integration/fixtures/options/index_eager_gate",
    .args = { "build" },
    .expect.locked = {
      { .name = "a", .version = "1.0.0" },
      { .name = "ghost", .version = "1.0.0" },
    },
  },
  {
    .name = "path_pins",
    .project = "test/integration/fixtures/lock/pair",
    .args = { "path", "A" },
    .expect.locked = {
      { .name = "A", .version = "1.0.0" },
      { .name = "B", .version = "1.0.0" },
    },
  },
  {
    .name = "moved_source_rejected",
    .project = "test/integration/fixtures/lock/moved",
    .copy = { "spn.lock" },
    .args = { "build" },
    .expect = { .rc = 1, .err = SPN_ERR_LOCK_MISMATCH },
  },
  {
    .name = "locked_rejects_moved_source",
    .project = "test/integration/fixtures/lock/moved",
    .copy = { "spn.lock" },
    .args = { "build", "--locked" },
    .expect = { .rc = 1, .err = SPN_ERR_LOCK_MISMATCH },
  },
  {
    .name = "locked_requires_lock",
    .project = "test/integration/fixtures/deps/file/basic",
    .copy = { "packages/*" },
    .args = { "build", "--locked" },
    .expect = { .rc = 1, .err = SPN_ERR_LOCK_STALE, .missing = { lit("spn.lock") } },
  },
  {
    .name = "locked_test",
    .project = "test/integration/fixtures/lock/pair",
    .args = { "test", "--locked" },
    .expect = { .rc = 1, .err = SPN_ERR_LOCK_STALE },
  },
  {
    .name = "locked_path",
    .project = "test/integration/fixtures/lock/pair",
    .args = { "path", "A", "--locked" },
    .expect = { .rc = 1, .err = SPN_ERR_LOCK_STALE },
  },
  {
    .name = "update_creates_lock",
    .project = "test/integration/fixtures/lock/pair",
    .args = { "update" },
    .expect.locked = {
      { .name = "A", .version = "1.0.0" },
      { .name = "B", .version = "1.0.0" },
    },
  },
  {
    .name = "invalid_rejected",
    .project = "test/integration/fixtures/lock/invalid",
    .copy = { "spn.lock" },
    .args = { "build" },
    .expect = { .rc = 1, .err = SPN_ERR_LOCK_INVALID },
  },
  {
    .name = "newer_version_rejected",
    .project = "test/integration/fixtures/lock/newer",
    .copy = { "spn.lock" },
    .args = { "build" },
    .expect = { .rc = 1, .err = SPN_ERR_LOCK_VERSION },
  },
};

sp_test_each(lock, commands, command_test_t, commands) {
  return run_command_test(t, *it);
}

static const rebuild_test_t rebuilds [] = {
  {
    .name = "holds_version",
    .project = "test/integration/fixtures/lock/pair",
    .copy = { "spn.wide.toml" },
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.moves = { { .from = "spn.wide.toml", .to = "spn.toml" } },
        .command = {
          .args = { "build" },
          .expect.locked = {
            { .name = "A", .version = "1.0.0" },
            { .name = "B", .version = "1.0.0" },
          },
        },
      },
    },
  },
  {
    .name = "drops_removed_deps",
    .project = "test/integration/fixtures/lock/pair",
    .copy = { "spn.less.toml" },
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.moves = { { .from = "spn.less.toml", .to = "spn.toml" } },
        .command = {
          .args = { "build" },
          .expect.locked = { { .name = "B", .version = "1.0.0" } },
        },
      },
    },
  },
  {
    .name = "locked_accepts_current",
    .project = "test/integration/fixtures/lock/pair",
    .first.args = { "build" },
    .rebuilds = {
      {
        .command.args = { "build", "--locked" },
      },
    },
  },
  {
    .name = "locked_rejects_stale",
    .project = "test/integration/fixtures/lock/pair",
    .copy = { "spn.less.toml" },
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.moves = { { .from = "spn.less.toml", .to = "spn.toml" } },
        .command = {
          .args = { "build", "--locked" },
          .expect = {
            .rc = 1,
            .err = SPN_ERR_LOCK_STALE,
            .locked = {
              { .name = "A", .version = "1.0.0" },
              { .name = "B", .version = "1.0.0" },
            },
          },
        },
      },
    },
  },
  {
    .name = "update_moves_all",
    .project = "test/integration/fixtures/lock/pair",
    .copy = { "spn.wide.toml" },
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.moves = { { .from = "spn.wide.toml", .to = "spn.toml" } },
        .command = {
          .args = { "update" },
          .expect.locked = {
            { .name = "A", .version = "1.1.0" },
            { .name = "B", .version = "1.1.0" },
          },
        },
      },
    },
  },
  {
    .name = "update_moves_named",
    .project = "test/integration/fixtures/lock/pair",
    .copy = { "spn.wide.toml" },
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.moves = { { .from = "spn.wide.toml", .to = "spn.toml" } },
        .command = {
          .args = { "update", "A" },
          .expect.locked = {
            { .name = "A", .version = "1.1.0" },
            { .name = "B", .version = "1.0.0" },
          },
        },
      },
    },
  },
  {
    .name = "update_reports_changes",
    .project = "test/integration/fixtures/lock/pair",
    .copy = { "spn.wide.toml" },
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.moves = { { .from = "spn.wide.toml", .to = "spn.toml" } },
        .command = {
          .args = { "update", "A" },
          .expect.events = {
            { .event = SPN_EVENT_LOCK_REMOVED, .key = "version", .value = "1.0.0" },
            { .event = SPN_EVENT_LOCK_ADDED, .key = "version", .value = "1.1.0" },
          },
        },
      },
    },
  },
  {
    .name = "update_accepts_moved_source",
    .project = "test/integration/fixtures/lock/moved",
    .copy = { "spn.lock" },
    .first.args = { "update" },
    .rebuilds = {
      {
        .command.args = { "build", "--locked" },
      },
    },
  },
  {
    .name = "update_leaves_unnamed",
    .project = "test/integration/fixtures/lock/moved",
    .copy = { "spn.lock" },
    .first.args = { "update", "B" },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect = { .rc = 1, .err = SPN_ERR_LOCK_MISMATCH },
        },
      },
    },
  },
  {
    .name = "update_named_converges",
    .project = "test/integration/fixtures/lock/moved",
    .copy = { "spn.lock" },
    .first.args = { "update", "A" },
    .rebuilds = {
      {
        .command.args = { "update", "B" },
      },
      {
        .command.args = { "build", "--locked" },
      },
    },
  },
  {
    .name = "update_unknown_name",
    .project = "test/integration/fixtures/lock/pair",
    .first.args = { "build" },
    .rebuilds = {
      {
        .command = {
          .args = { "update", "C" },
          .expect = { .rc = 1, .err = SPN_ERR_LOCK_ABSENT },
        },
      },
    },
  },
  {
    .name = "locked_across_profiles",
    .project = "test/integration/fixtures/options/gates_dep",
    .first.args = { "build" },
    .rebuilds = {
      {
        .command.args = { "build", "-p", "tracing", "--locked" },
      },
    },
  },
  {
    .name = "two_versions",
    .project = "test/integration/fixtures/lock/versions",
    .first = {
      .args = { "build" },
      .expect.locked = {
        { .name = "B", .version = "1.0.0" },
        { .name = "B", .version = "2.0.0" },
        { .name = "T", .version = "1.0.0" },
      },
    },
    .rebuilds = {
      {
        .command.args = { "build", "--locked" },
      },
    },
  },
};

sp_test_each(lock, rebuild, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}
