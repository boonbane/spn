#include "harness.h"

static const test_t tests [] = {
  {
    .name = "staged_bin_loads_its_libs",
    .project = "test/integration/fixtures/layout/test_shared",
    .copy = { "check.c", "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EXISTS, .exists = staged_lib("spum") },
      { .kind = ACTION_RUN_BIN, .bin.file = exe("main") },
      { .kind = ACTION_STAGE_BARE_RUN, .bare = { .file = exe("main"), .with = { staged_lib("spum") }, .expect = BARE_EXPECT_RUNS } },
    },
  },
};

sp_test_each(layout, cases, test_t, tests) {
  return run_test(t, *it);
}

static const command_test_t commands [] = {
  {
    .name = "staged_test",
    .project = "test/integration/fixtures/layout/test_shared",
    .copy = { "check.c", "packages/*" },
    .args = { "build" },
    .expect = {
      .bin.path = test_exe("check"),
      .exists = { test_exe("check"), test_lib("spum") },
    },
  },
  {
    .name = "staged_script",
    .project = "test/integration/fixtures/script/staged",
    .args = { "build", "main" },
    .expect.exists = { exe("main") },
  },
  {
    .name = "reserved_bin_name",
    .project = "test/integration/fixtures/layout/reserved_bin",
    .args = { "build" },
    .expect.rc = 1,
  },
};

sp_test_each(layout, commands, command_test_t, commands) {
  return run_command_test(t, *it);
}

static const rebuild_test_t rebuilds [] = {
  {
    .name = "staged_prune",
    .project = "test/integration/fixtures/freshness/shared",
    .copy = { "packages/*", "t.c", "spn.nodep.toml", "main.nodep.c" },
    .first = {
      .args = { "build" },
      .expect.exists = { exe("main"), staged_lib("B") },
    },
    .rebuilds = {
      {
        .change = {
          .moves = {
            { .from = "spn.nodep.toml", .to = "spn.toml" },
            { .from = "main.nodep.c", .to = "main.c" },
          },
          .writes = { { .file = "build/debug/user.txt", .content = "U" } },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { exe("main"), lit("build/debug/user.txt") },
            .missing = { staged_lib("B") },
          },
        },
      },
    },
  },
  {
    .name = "staged_selection_keeps_others",
    .project = "test/integration/fixtures/freshness/shared",
    .copy = { "packages/*", "t.c" },
    .first = {
      .args = { "build" },
      .expect.exists = { exe("main"), staged_lib("B") },
    },
    .rebuilds = {
      {
        .command = {
          .args = { "build", "--test" },
          .expect.exists = { exe("main"), staged_lib("B"), test_exe("T") },
        },
      },
    },
  },
};

sp_test_each(layout, rebuild, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}

// macOS dyld resolves a hardlinked main executable to an arbitrary one of the
// inode's link names, so @loader_path is only well-defined when a staged exe
// is its inode's sole name.
sp_test(layout, staged_identity) {
  fixture_t fixture = sp_zero;
  sp_try(fixture_init(t, &fixture));
  sp_try(test_when(t, sp_zero_struct(test_when_t)));
  sp_try(run_command(t, &fixture, (command_test_t) {
    .project = "test/integration/fixtures/layout/test_shared",
    .copy = { "check.c", "packages/*" },
    .args = { "build" },
    .expect.exists = { exe("main"), test_exe("check") },
  }));

  path_t staged [] = { exe("main"), staged_lib("spum"), test_exe("check"), test_lib("spum") };
  sp_carr_for(staged, it) {
    sp_str_t path = fixture_path(&fixture, path_render(&fixture, staged[it]));
    sp_sys_file_meta_t meta = sp_zero;
    sp_sys_get_path_metadata_s(sp_sys_get_root(0), path, &meta);
    sp_test_kv(t, "path", path);
    sp_expect_eq(t, (u64)1, meta.nlink);
  }
  return SP_OK;
}

sp_test(layout, target_triple) {
  const c8* triple = test_host_triple();
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/layout/test_shared",
    .copy = { "check.c", "packages/*" },
    .args = { "build", "--target", triple },
    .expect = {
      .exists = { target_exe("main", triple) },
      .missing = { lit("build/debug") },
    },
  });
}
