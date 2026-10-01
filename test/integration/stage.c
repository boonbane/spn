#include "harness.h"

sp_test(stage, file) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/stage/file",
    .copy = { "I.txt" },
    .args = { "build" },
    .expect.files = {
      { .file = sp_str_lit("gen/G.txt"), .content = "A" },
      { .file = sp_str_lit("build/.spn/staged"), .content = "gen/G.txt\tgen/G.txt\n" },
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
      { .file = sp_str_lit("build/.spn/staged"), .content = "gen\tgen/a.txt\ngen\tgen/d.txt\n" },
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
        .change.remove_dirs = { sp_str_lit("build") },
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

sp_test(stage, dep_ignored) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/stage/dep",
    .copy = { "packages/*" },
    .args = { "build" },
    .expect.missing = { sp_str_lit("gen"), sp_str_lit("packages/D/gen") },
  });
}

typedef struct {
  const c8* name;
  spn_err_t err;
} failure_t;

static const failure_t failures [] = {
  { .name = "unproduced", .err = SPN_ERR_STAGE_UNPRODUCED },
  { .name = "overlap", .err = SPN_ERR_STAGE_OVERLAP },
};

sp_test_each(stage, failure, failure_t, failures) {
  return run_command_test(t, (command_test_t) {
    .project = sp_str_to_cstr(sp_test_arena(t), sp_fmt(sp_test_arena(t), "test/integration/fixtures/stage/{}", sp_fmt_cstr(it->name)).value),
    .args = { "build" },
    .expect = { .rc = 1, .err = it->err },
  });
}
