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

sp_test(stage, source_glob) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/glob",
    .first.args = { "build" },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect.cc = { { .args = { "gen/G.c" }, .absent = true } },
        },
      },
    },
  });
}

sp_test(stage, recursive_glob) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/recursive_glob",
    .first.args = { "build" },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect.cc = { { .args = { "G.c" }, .absent = true } },
        },
      },
    },
  });
}

sp_test(stage, copy_glob_skips_staged) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/observed_listing",
    .copy = { "H" },
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.writes = { { .file = sp_str_lit("H/B"), .content = sp_str_lit("B") } },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { sp_str_lit("H/S/B") },
            .missing = { sp_str_lit("H/S/S") },
          },
        },
      },
    },
  });
}

typedef struct {
  const c8* name;
  const c8* copy;
} hit_t;

static const hit_t hits [] = {
  { .name = "observed_listing", .copy = "H" },
  { .name = "readdir", .copy = "H" },
  { .name = "stat" },
};

sp_test_each(stage, hits, hit_t, hits) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = sp_str_to_cstr(sp_test_arena(t), sp_fmt(sp_test_arena(t), "test/integration/fixtures/stage/{}", sp_fmt_cstr(it->name)).value),
    .copy = { it->copy },
    .first.args = { "build" },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect.events = { { .event = SPN_EVENT_BUILD_PASSED, .key = "misses", .value = "0" } },
        },
      },
    },
  });
}

sp_test(stage, observed_hit) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/stage/observed_hit",
    .copy = { "gen", "spn.staged.toml" },
    .first.args = { "build" },
    .rebuilds = {
      {
        .change.moves = {
          { .from = sp_str_lit("spn.staged.toml"), .to = sp_str_lit("spn.toml") },
        },
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
  { .name = "observed_commands", .err = SPN_ERR_STAGE_OBSERVED },
  { .name = "observed_lock", .err = SPN_ERR_STAGE_OBSERVED },
};

sp_test_each(stage, failure, failure_t, failures) {
  return run_command_test(t, (command_test_t) {
    .project = sp_str_to_cstr(sp_test_arena(t), sp_fmt(sp_test_arena(t), "test/integration/fixtures/stage/{}", sp_fmt_cstr(it->name)).value),
    .copy = { it->copy },
    .args = { "build" },
    .expect = { .rc = 1, .err = it->err },
  });
}

typedef struct {
  const c8* name;
  const c8* copy;
} parent_t;

static const parent_t parents [] = {
  { .name = "listing", .copy = "A" },
  { .name = "probe" },
};

sp_test_each(stage, parent, parent_t, parents) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = sp_str_to_cstr(sp_test_arena(t), sp_fmt(sp_test_arena(t), "test/integration/fixtures/stage/parent_{}", sp_fmt_cstr(it->name)).value),
    .copy = { it->copy },
    .first.args = { "build" },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect.events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true } },
        },
      },
    },
  });
}

sp_test(stage, parent_kept_when_build_fails) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/stage/parent_failure",
    .args = { "build" },
    .expect = { .rc = 1, .exists = { sp_str_lit("A/B") } },
  });
}
