#include "harness.h"

sp_test(layout, staged_bin) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/layout/test_shared",
    .copy = { "check.c", "packages/*" },
    .args = { "build" },
    .expect = {
      .bin.name = "main",
      .exists = { exe("main"), staged_lib("spum") },
    },
  });
}

sp_test(layout, staged_test) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/layout/test_shared",
    .copy = { "check.c", "packages/*" },
    .args = { "build" },
    .expect = {
      .bin.path = test_exe("check"),
      .exists = { test_exe("check"), test_lib("spum") },
    },
  });
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

  sp_str_t staged[] = { exe("main"), staged_lib("spum"), test_exe("check"), test_lib("spum") };
  sp_carr_for(staged, it) {
    sp_str_t path = fixture_path(&fixture, staged[it]);
    sp_sys_file_meta_t meta = sp_zero;
    sp_sys_get_path_metadata_s(sp_sys_get_root(0), path, &meta);
    sp_test_kv(t, "path", path);
    sp_expect_eq(t, (u64)1, meta.nlink);
  }
  return SP_OK;
}

sp_test(layout, staged_collision) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/layout/staged_collision",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli = { "build", .rc = 1 } },
      { .kind = ACTION_VERIFY_RESULT, .verify_result = { .err = SPN_ERR_TARGET_COLLISION } },
    },
  });
}

sp_test(layout, staged_prune) {
  return run_rebuild_test(t, (rebuild_test_t) {
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
            { .from = sp_str_lit("spn.nodep.toml"), .to = sp_str_lit("spn.toml") },
            { .from = sp_str_lit("main.nodep.c"), .to = sp_str_lit("main.c") },
          },
          .writes = { { .file = sp_str_lit("build/debug/user.txt"), .content = sp_str_lit("U") } },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { exe("main"), sp_str_lit("build/debug/user.txt") },
            .missing = { staged_lib("B") },
          },
        },
      },
    },
  });
}

sp_test(layout, staged_selection_keeps_others) {
  return run_rebuild_test(t, (rebuild_test_t) {
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
  });
}

sp_test(layout, staged_script) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/staged",
    .args = { "build", "main" },
    .expect.exists = { exe("main") },
  });
}

sp_test(layout, script_ctx_footprint) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/default_script",
    .args = { "build" },
    .expect = {
      .exists = { sp_str_lit("build/wasm32-wasi-musl/.spn/default_script/configure.wasm") },
    },
  });
}

sp_test(layout, reserved_bin_name) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/layout/reserved_bin",
    .args = { "build" },
    .expect.rc = 1,
  });
}

sp_test(layout, target_triple) {
  const c8* triple = test_host_triple();
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/layout/test_shared",
    .copy = { "check.c", "packages/*" },
    .args = { "build", "--target", triple },
    .expect = {
      .exists = { target_exe("main", triple) },
      .missing = { sp_str_lit("build/debug") },
    },
  });
}
