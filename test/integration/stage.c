#include "harness.h"

static const command_test_t commands [] = {
  {
    .name = "file",
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .args = { "build" },
    .expect.files = { { lit("gen/G.txt"), .content = "A" } },
  },
  {
    .name = "tree",
    .project = "test/integration/fixtures/stage/tree",
    .copy = { "H" },
    .args = { "build" },
    .expect.files = {
      { lit("gen/a.txt"), .content = "A" },
      { lit("gen/d.txt"), .content = "D" },
    },
  },
  {
    .name = "store",
    .project = "test/integration/fixtures/stage/store",
    .args = { "build" },
    .expect.files = { { lit("gen/G.txt"), .content = "A" } },
  },
  {
    .name = "dep_ignored",
    .project = "test/integration/fixtures/stage/dep",
    .copy = { "packages/*" },
    .args = { "build" },
    .expect.missing = { lit("gen"), lit("packages/D/gen") },
  },
  {
    .name = "lists_root_beside_staged_file",
    .project = "test/integration/fixtures/stage/list_root",
    .args = { "build" },
    .expect.files = { { lit("gen/G.txt"), .content = "A" } },
  },
};

sp_test_each(stage, commands, command_test_t, commands) {
  return run_command_test(t, *it);
}

static const rebuild_test_t rebuilds [] = {
  {
    .name = "replay",
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .first = {
      .args = { "build" },
      .expect.events = { { .event = SPN_EVENT_SCRIPT_USER_FN } },
    },
    .rebuilds = {
      {
        .change = {
          .remove_files = { lit("gen/G.txt") },
          .remove_dirs = { "build" },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true } },
            .files = { { lit("gen/G.txt"), .content = "A" } },
          },
        },
      },
      {
        .change.remove_files = { lit("gen/G.txt") },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true } },
            .files = { { lit("gen/G.txt"), .content = "A" } },
          },
        },
      },
    },
  },
  {
    .name = "overwrite",
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .first = {
      .args = { "build" },
      .expect.files = { { lit("gen/G.txt"), .content = "A" } },
    },
    .rebuilds = {
      {
        .change = {
          .remove_dirs = { "build" },
          .writes = { { .file = "I.txt", .content = "B" } },
        },
        .command = {
          .args = { "build" },
          .expect.files = { { lit("gen/G.txt"), .content = "B" } },
        },
      },
    },
  },
  {
    .name = "prune",
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt", "spn.unstaged.toml" },
    .first = {
      .args = { "build" },
      .expect.exists = { lit("gen/G.txt") },
    },
    .rebuilds = {
      {
        .change.moves = { { .from = "spn.unstaged.toml", .to = "spn.toml" } },
        .command = {
          .args = { "build" },
          .expect.missing = { lit("gen") },
        },
      },
    },
  },
  {
    .name = "unchanged_when_build_fails",
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .first = {
      .args = { "build" },
      .expect.files = { { lit("gen/G.txt"), .content = "A" } },
    },
    .rebuilds = {
      {
        .change.writes = {
          { .file = "I.txt", .content = "B" },
          { .file = "main.c", .content = "int main( {" },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .rc = 1,
            .files = { { lit("gen/G.txt"), .content = "A" } },
          },
        },
      },
    },
  },
  {
    .name = "tree_drops_file",
    .project = "test/integration/fixtures/stage/tree",
    .copy = { "H" },
    .first = {
      .args = { "build" },
      .expect.exists = { lit("gen/a.txt"), lit("gen/d.txt") },
    },
    .rebuilds = {
      {
        .change.remove_files = { lit("H/d.txt") },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { lit("gen/a.txt") },
            .missing = { lit("gen/d.txt") },
          },
        },
      },
    },
  },
  {
    .name = "prune_keeps_unlisted",
    .project = "test/integration/fixtures/stage/tree",
    .copy = { "H", "spn.unstaged.toml" },
    .first = {
      .args = { "build" },
      .expect.exists = { lit("gen/a.txt"), lit("gen/d.txt") },
    },
    .rebuilds = {
      {
        .change = {
          .remove_files = { lit("H/d.txt") },
          .writes = { { .file = "gen/U.txt", .content = "U" } },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { lit("gen/U.txt") },
            .missing = { lit("gen/d.txt") },
          },
        },
      },
      {
        .change.moves = { { .from = "spn.unstaged.toml", .to = "spn.toml" } },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { lit("gen/U.txt") },
            .missing = { lit("gen/a.txt") },
          },
        },
      },
    },
  },
  {
    .name = "profiles",
    .project = "test/integration/fixtures/stage/profile",
    .first = {
      .args = { "build" },
      .expect.files = { { lit("gen/G.txt"), .content = "A" } },
    },
    .rebuilds = {
      {
        .command = {
          .args = { "build", "-p", "release" },
          .expect = {
            .exists = { exe("P"), profile_exe("release", "P") },
            .files = { { lit("gen/G.txt"), .content = "B" } },
          },
        },
      },
    },
  },
  {
    .name = "source_glob",
    .project = "test/integration/fixtures/stage/glob",
    .first.args = { "build" },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect = { .rc = 1, .err = SPN_ERR_STAGE_OVERLAP },
        },
      },
    },
  },
  {
    .name = "observed_hit",
    .project = "test/integration/fixtures/stage/observed_hit",
    .copy = { "gen", "spn.staged.toml" },
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.moves = { { .from = "spn.staged.toml", .to = "spn.toml" } },
        .command = {
          .args = { "build" },
          .expect = {
            .rc = 1,
            .err = SPN_ERR_STAGE_OBSERVED,
            .events = { { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true } },
          },
        },
      },
    },
  },
};

sp_test_each(stage, rebuilds, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}

static const test_t cleans [] = {
  {
    .name = "clean",
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("gen/G.txt") },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("compile_commands.json") },
      { .kind = ACTION_RUN_CLI, .cli.cmd = "clean" },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = lit("gen") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = lit(".spn") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = lit("compile_commands.json") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = lit("build") },
    },
  },
  {
    .name = "clean_profile_keeps_stages",
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = lit("gen/G.txt") },
    },
  },
};

sp_test_each(stage, cleans, test_t, cleans) {
  return run_test(t, *it);
}

typedef struct {
  const c8* name;
  const c8* copy;
  spn_err_t err;
} failure_t;

static const failure_t failures [] = {
  { .name = "unproduced", .err = SPN_ERR_STAGE_UNPRODUCED },
  { .name = "overlap", .err = SPN_ERR_STAGE_OVERLAP },
  { .name = "observed_node", .err = SPN_ERR_STAGE_OBSERVED },
  { .name = "observed_header", .copy = "gen", .err = SPN_ERR_STAGE_OBSERVED },
  { .name = "observed_commands", .err = SPN_ERR_STAGE_OBSERVED },
  { .name = "list_staged", .err = SPN_ERR_STAGE_OBSERVED },
  { .name = "collision", .copy = "gen", .err = SPN_ERR_STAGE_COLLISION },
};

sp_test_each(stage, failures, failure_t, failures) {
  return run_command_test(t, (command_test_t) {
    .project = sp_str_to_cstr(sp_test_arena(t), sp_fmt(sp_test_arena(t), "test/integration/fixtures/stage/{}", sp_fmt_cstr(it->name)).value),
    .copy = { it->copy },
    .args = { "build" },
    .expect = { .rc = 1, .err = it->err },
  });
}
