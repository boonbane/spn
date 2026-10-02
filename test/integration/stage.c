#include "harness.h"

sp_test(stage, file) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .args = { "build" },
    .expect.files = {
      { .file = sp_str_lit("gen/G.txt"), .content = "A" },
    },
  });
}

sp_test(stage, tree) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/stage/tree",
    .copy = { "H" },
    .args = { "build" },
    .expect.files = {
      { .file = sp_str_lit("gen/a.txt"), .content = "A" },
      { .file = sp_str_lit("gen/d.txt"), .content = "D" },
    },
  });
}

sp_test(stage, replay) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .first = {
      .args = { "build" },
      .expect.events = { { .event = SPN_EVENT_SCRIPT_USER_FN } },
    },
    .rebuilds = {
      {
        .change = {
          .remove_files = { sp_str_lit("gen/G.txt") },
          .remove_dirs = { sp_str_lit("build") },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true } },
            .files = { { .file = sp_str_lit("gen/G.txt"), .content = "A" } },
          },
        },
      },
      {
        .change.remove_files = { sp_str_lit("gen/G.txt") },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true } },
            .files = { { .file = sp_str_lit("gen/G.txt"), .content = "A" } },
          },
        },
      },
    },
  });
}

sp_test(stage, overwrite) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .first = {
      .args = { "build" },
      .expect.files = { { .file = sp_str_lit("gen/G.txt"), .content = "A" } },
    },
    .rebuilds = {
      {
        .change = {
          .remove_dirs = { sp_str_lit("build") },
          .writes = { { .file = sp_str_lit("I.txt"), .content = sp_str_lit("B") } },
        },
        .command = {
          .args = { "build" },
          .expect.files = { { .file = sp_str_lit("gen/G.txt"), .content = "B" } },
        },
      },
    },
  });
}

sp_test(stage, prune) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt", "spn.unstaged.toml" },
    .first = {
      .args = { "build" },
      .expect.exists = { sp_str_lit("gen/G.txt") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = sp_str_lit("spn.unstaged.toml"), .to = sp_str_lit("spn.toml") },
        },
        .command = {
          .args = { "build" },
          .expect.missing = { sp_str_lit("gen") },
        },
      },
    },
  });
}

sp_test(stage, unchanged_when_build_fails) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .first = {
      .args = { "build" },
      .expect.files = { { .file = sp_str_lit("gen/G.txt"), .content = "A" } },
    },
    .rebuilds = {
      {
        .change.writes = {
          { .file = sp_str_lit("I.txt"), .content = sp_str_lit("B") },
          { .file = sp_str_lit("main.c"), .content = sp_str_lit("int main( {") },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .rc = 1,
            .files = { { .file = sp_str_lit("gen/G.txt"), .content = "A" } },
          },
        },
      },
    },
  });
}

sp_test(stage, tree_drops_file) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/tree",
    .copy = { "H" },
    .first = {
      .args = { "build" },
      .expect.exists = { sp_str_lit("gen/a.txt"), sp_str_lit("gen/d.txt") },
    },
    .rebuilds = {
      {
        .change.remove_files = { sp_str_lit("H/d.txt") },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { sp_str_lit("gen/a.txt") },
            .missing = { sp_str_lit("gen/d.txt") },
          },
        },
      },
    },
  });
}

sp_test(stage, prune_keeps_unlisted) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/tree",
    .copy = { "H", "spn.unstaged.toml" },
    .first = {
      .args = { "build" },
      .expect.exists = { sp_str_lit("gen/a.txt"), sp_str_lit("gen/d.txt") },
    },
    .rebuilds = {
      {
        .change = {
          .remove_files = { sp_str_lit("H/d.txt") },
          .writes = { { .file = sp_str_lit("gen/U.txt"), .content = sp_str_lit("U") } },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { sp_str_lit("gen/U.txt") },
            .missing = { sp_str_lit("gen/d.txt") },
          },
        },
      },
      {
        .change.moves = {
          { .from = sp_str_lit("spn.unstaged.toml"), .to = sp_str_lit("spn.toml") },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { sp_str_lit("gen/U.txt") },
            .missing = { sp_str_lit("gen/a.txt") },
          },
        },
      },
    },
  });
}

sp_test(stage, target_output) {
  sp_str_t staged = sp_fs_get_name(exe("B"));
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/target",
    .copy = { "t.c" },
    .first = {
      .args = { "build" },
      .expect.exists = { staged },
    },
    .rebuilds = {
      {
        .command = {
          .args = { "test" },
          .expect.exists = { staged },
        },
      },
    },
  });
}

sp_test(stage, profiles) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/profile",
    .first = {
      .args = { "build" },
      .expect.files = { { .file = sp_str_lit("gen/G.txt"), .content = "A" } },
    },
    .rebuilds = {
      {
        .command = {
          .args = { "build", "-p", "release" },
          .expect = {
            .exists = { exe("P"), profile_exe("release", "P") },
            .files = { { .file = sp_str_lit("gen/G.txt"), .content = "B" } },
          },
        },
      },
    },
  });
}

sp_test(stage, dep_ignored) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/stage/dep",
    .copy = { "packages/*" },
    .args = { "build" },
    .expect.missing = { sp_str_lit("gen"), sp_str_lit("packages/D/gen") },
  });
}

sp_test(stage, clean) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = sp_str_lit("gen/G.txt") },
      { .kind = ACTION_RUN_CLI, .cli.cmd = "clean" },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = sp_str_lit("gen") },
      { .kind = ACTION_VERIFY_NOT_EXISTS, .exists = sp_str_lit("build") },
    },
  });
}

sp_test(stage, clean_profile_keeps_stages) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "clean", .args = { "-p", "debug" } } },
      { .kind = ACTION_VERIFY_EXISTS, .exists = sp_str_lit("gen/G.txt") },
    },
  });
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
};

sp_test_each(stage, failure, failure_t, failures) {
  return run_command_test(t, (command_test_t) {
    .project = sp_str_to_cstr(sp_test_arena(t), sp_fmt(sp_test_arena(t), "test/integration/fixtures/stage/{}", sp_fmt_cstr(it->name)).value),
    .copy = { it->copy },
    .args = { "build" },
    .expect = { .rc = 1, .err = it->err },
  });
}
