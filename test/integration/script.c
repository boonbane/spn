#include "harness.h"

static const command_test_t graphs [] = {
  { .name = "basic_node", .project = "test/integration/fixtures/script/basic_node", .args = { "build" } },
  { .name = "tree_output", .project = "test/integration/fixtures/script/tree_output", .args = { "build" } },
  { .name = "diamond_deps", .project = "test/integration/fixtures/script/diamond_deps", .args = { "build" } },
  { .name = "multi_output", .project = "test/integration/fixtures/script/multi_output", .args = { "build" } },
  { .name = "node_linking", .project = "test/integration/fixtures/script/node_linking", .args = { "build" } },
  { .name = "orphan_outputs", .project = "test/integration/fixtures/script/orphan_outputs", .args = { "build" }, .expect.exists = { work_file("O/O.h") } },
  { .name = "observed_list_root", .project = "test/integration/fixtures/script/observed_list_root", .args = { "build" } },
};

sp_test_each(script, graph, command_test_t, graphs) {
  return run_command_test(t, *it);
}

static const command_test_t work_trees [] = {
  { .name = "source", .project = "test/integration/fixtures/script/work_source", .args = { "build" }, .expect.exists = { exe("A") } },
  { .name = "include", .project = "test/integration/fixtures/script/work_include", .args = { "build" }, .expect.exists = { exe("A") } },
  { .name = "embed", .project = "test/integration/fixtures/script/work_embed", .args = { "build" }, .expect.exists = { exe("A") } },
  { .name = "embed_dir", .project = "test/integration/fixtures/script/work_embed_dir", .args = { "build" }, .expect.exists = { exe("A") } },
};

sp_test_each(script, work_tree, command_test_t, work_trees) {
  return run_command_test(t, *it);
}

static const command_test_t apis [] = {
  {
    .name = "configure_table",
    .project = "test/integration/fixtures/script/configure_table",
    .copy = { "include" },
    .args = { "build" },
    .expect.exists = { exe("configure_table") },
  },
  {
    .name = "configure_unmounted",
    .project = "test/integration/fixtures/script/configure_unmounted",
    .args = { "build" },
  },
  {
    .name = "program_name_context",
    .project = "test/integration/fixtures/script/module_name_collision",
    .args = { "build" },
    .expect.exists = { exe("configure") },
  },
  {
    .name = "shared_script",
    .project = "test/integration/fixtures/script/shared_script",
    .args = { "build" },
    .expect.files = { { work_file("S/V.txt"), .content = "1" } },
  },
  {
    .name = "node_output_bin",
    .project = "test/integration/fixtures/script/node_output_bin",
    .args = { "build" },
    .expect.exists = { pkg_store_file("B", "bin/R.txt") },
  },
  {
    .name = "object_lib",
    .project = "test/integration/fixtures/script/object_lib",
    .copy = { "packages/*" },
    .args = { "build" },
    .expect.exists = { pkg_store_file("spum", "lib/manifest/rt/extra.c.o"), pkg_static_lib("spum", "blob"), exe("object_lib") },
  },
  {
    .name = "cross_package",
    .project = "test/integration/fixtures/script/cross_package",
    .args = { "build" },
    .expect = {
      .exists = { exe("cross_package") },
      .locked = { { .name = "spum" } },
    },
  },
  {
    .name = "embed_cross_linux_aarch64",
    .project = "test/integration/fixtures/target/embed",
    .copy = { "A.txt" },
    .when.target = "aarch64-linux-musl",
    .args = { "build", "--target", "aarch64-linux-musl" },
    .expect.exists = { target_exe("main", "aarch64-linux-musl") },
  },
  {
    .name = "embed_cross_windows_aarch64",
    .project = "test/integration/fixtures/target/embed",
    .copy = { "A.txt" },
    .when.target = "aarch64-windows-gnu",
    .args = { "build", "--target", "aarch64-windows-gnu" },
    .expect.exists = { target_exe("main", "aarch64-windows-gnu") },
  },
};

sp_test_each(script, api, command_test_t, apis) {
  return run_command_test(t, *it);
}

static const test_t embeds [] = {
  {
    .name = "manifest",
    .project = "test/integration/fixtures/target/embed",
    .copy = { "A.txt" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin.file = exe("main") },
    },
  },
};

sp_test_each(script, embed, test_t, embeds) {
  return run_test(t, *it);
}

static const rebuild_test_t reruns [] = {
  {
    .name = "chained_nodes",
    .project = "test/integration/fixtures/script/chained_nodes",
    .copy = { "a.txt", "a.change.txt" },
    .first = {
      .args = { "build" },
      .expect.files = { { work_file("C/B.txt"), .content = "2" } },
    },
    .rebuilds = {
      {
        .change.moves = { { .from = "a.change.txt", .to = "a.txt" } },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "B" } },
            .files = { { work_file("C/B.txt"), .content = "6" } },
          },
        },
      },
    },
  },
  {
    .name = "script_edit",
    .project = "test/integration/fixtures/script/script_edit",
    .first = {
      .args = { "build" },
      .expect.files = { { work_file("E/V.txt"), .content = "1" } },
    },
    .rebuilds = {
      {
        .change.moves = { { .from = "tools/build.change.c", .to = "tools/build.c" } },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "gen" } },
            .files = { { work_file("E/V.txt"), .content = "2" } },
          },
        },
      },
    },
  },
  {
    .name = "tree_output_cached",
    .project = "test/integration/fixtures/script/tree_output",
    .first = {
      .args = { "build" },
      .expect.events = { { .event = SPN_EVENT_SCRIPT_USER_FN } },
    },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect.events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true } },
        },
      },
    },
  },
  {
    .name = "tree_output_rerun_drops_file",
    .project = "test/integration/fixtures/script/tree_output_drop",
    .copy = { "H" },
    .first = {
      .args = { "build" },
      .expect.exists = { work_file("M/gen/G/a.h"), work_file("M/gen/G/d.h") },
    },
    .rebuilds = {
      {
        .change.remove_files = { lit("H/d.h") },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { work_file("M/gen/G/a.h") },
            .missing = { work_file("M/gen/G/d.h") },
          },
        },
      },
    },
  },
  {
    .name = "build_script_regenerates_deleted_output",
    .project = "test/integration/fixtures/script/build_script",
    .copy = { "include", "vendor" },
    .first = {
      .args = { "build" },
      .expect.exists = { exe("build_script"), work_file("build_script/version.h") },
    },
    .rebuilds = {
      {
        .change.remove_files = { work_file("build_script/version.h") },
        .command = {
          .args = { "build" },
          .expect.exists = { work_file("build_script/version.h") },
        },
      },
    },
  },
  {
    .name = "build_deps_replay",
    .project = "test/integration/fixtures/script/build_deps",
    .first = {
      .args = { "build" },
      .expect.exists = { exe("build_deps") },
    },
    .rebuilds = {
      {
        .change.remove_dirs = { "build" },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true } },
            .exists = { exe("build_deps") },
          },
        },
      },
    },
  },
  {
    .name = "publish_replay",
    .project = "test/integration/fixtures/script/publish_replay",
    .copy = { "packages/*" },
    .first = {
      .args = { "build" },
      .expect = {
        .events = { { .event = SPN_EVENT_SCRIPT_USER_FN } },
        .exists = { pkg_store_file("kit", "include/kit.h"), exe("main") },
      },
    },
    .rebuilds = {
      {
        .change.remove_dirs = { "build" },
        .command = {
          .args = { "build" },
          .expect = {
            .events = {
              { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true },
              { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
            },
            .exists = { pkg_store_file("kit", "include/kit.h"), exe("main") },
          },
        },
      },
    },
  },
  {
    .name = "embed_dir",
    .project = "test/integration/fixtures/script/embed_dir",
    .copy = { "H" },
    .first = {
      .args = { "build" },
      .expect = {
        .events = { { .event = SPN_EVENT_SCRIPT_USER_FN } },
        .exists = { exe("M") },
      },
    },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect.events = {
            { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true },
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
          },
        },
      },
      {
        .change.moves = { { .from = "H/a.change.h", .to = "H/a.h" } },
        .command = {
          .args = { "build" },
          .expect.events = {
            { .event = SPN_EVENT_SCRIPT_USER_FN },
            { .event = SPN_EVENT_EMBED_PASSED },
          },
        },
      },
    },
    .watches = {
      { .file = exe("M"), .mtime = REBUILD_MTIME_CHANGED },
    },
  },
  {
    .name = "observed_read",
    .project = "test/integration/fixtures/script/observed_read",
    .copy = { "X" },
    .first = {
      .args = { "build" },
      .expect.files = { { work_file("A/N.h"), .content = "#define A\n" } },
    },
    .rebuilds = {
      {
        .change.writes = { { .file = "X", .content = "B" } },
        .command = {
          .args = { "build" },
          .expect = {
            .events = {
              { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "N" },
              { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "M" },
            },
            .files = { { work_file("A/N.h"), .content = "#define B\n" } },
          },
        },
      },
    },
  },
  {
    .name = "observed_list",
    .project = "test/integration/fixtures/script/observed_list",
    .copy = { "D" },
    .first = {
      .args = { "build" },
      .expect.files = { { work_file("A/N"), .content = "A" } },
    },
    .rebuilds = {
      {
        .change.writes = { { .file = "D/B", .content = "B" } },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "N" } },
            .files = { { work_file("A/N"), .content = "AB" } },
          },
        },
      },
      {
        .change.remove_files = { lit("D/A") },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "N" } },
            .files = { { work_file("A/N"), .content = "B" } },
          },
        },
      },
    },
  },
  {
    .name = "observed_open_absent",
    .project = "test/integration/fixtures/script/observed_open_absent",
    .first = {
      .args = { "build" },
      .expect.files = { { work_file("A/N"), .content = "A" } },
    },
    .rebuilds = {
      {
        .change.writes = { { .file = "X", .content = "B" } },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "N" } },
            .files = { { work_file("A/N"), .content = "B" } },
          },
        },
      },
    },
  },
  {
    .name = "observed_stat_absent",
    .project = "test/integration/fixtures/script/observed_stat_absent",
    .first = {
      .args = { "build" },
      .expect.files = { { work_file("A/N"), .content = "A" } },
    },
    .rebuilds = {
      {
        .change.writes = { { .file = "X", .content = "B" } },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "N" } },
            .files = { { work_file("A/N"), .content = "B" } },
          },
        },
      },
    },
  },
  {
    .name = "observed_manifest_read",
    .project = "test/integration/fixtures/script/observed_manifest",
    .copy = { "X" },
    .first = {
      .args = { "build" },
      .expect.files = { { work_file("A/N"), .content = "A" } },
    },
    .rebuilds = {
      {
        .change.writes = { { .file = "X", .content = "B" } },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "N" } },
            .files = { { work_file("A/N"), .content = "B" } },
          },
        },
      },
    },
  },
  {
    .name = "observed_unrelated",
    .project = "test/integration/fixtures/script/observed_read",
    .copy = { "X", "Y" },
    .first = {
      .args = { "build" },
      .expect.events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "N" } },
    },
    .rebuilds = {
      {
        .change.writes = { { .file = "Y", .content = "B" } },
        .command = {
          .args = { "build" },
          .expect.events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true } },
        },
      },
      {
        .change.writes = { { .file = "Z", .content = "A" } },
        .command = {
          .args = { "build" },
          .expect.events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true } },
        },
      },
    },
  },
};

sp_test_each(script, rerun, rebuild_test_t, reruns) {
  return run_rebuild_test(t, *it);
}

static const identity_test_t identities [] = {
  {
    .name = "build_deps_lock",
    .project = "test/integration/fixtures/script/build_deps",
    .builds = { { "build" } },
    .expect.locked = { { .name = "spum" } },
  },
  {
    .name = "build_deps_get_one_store_entry",
    .project = "test/integration/fixtures/script/build_deps",
    .builds = { { "build" } },
    .expect.store = { { "spum", 1 } },
  },
  {
    .name = "build_deps_do_not_configure",
    .project = "test/integration/fixtures/script/build_deps",
    .builds = { { "build" } },
    .expect.configured = { { "spum", 0 } },
  },
  {
    .name = "build_dep_closure_locks",
    .project = "test/integration/fixtures/script/build_dep_closure",
    .builds = { { "build" } },
    .expect.locked = { { .name = "alpha" }, { .name = "beta" } },
  },
  {
    .name = "build_dep_closure_gets_one_store_entry_each",
    .project = "test/integration/fixtures/script/build_dep_closure",
    .builds = { { "build" } },
    .expect.store = { { "alpha", 1 }, { "beta", 1 } },
  },
  {
    .name = "build_dep_static_gets_one_store_entry",
    .project = "test/integration/fixtures/script/build_dep_static",
    .builds = { { "build" } },
    .expect.store = { { "spum", 1 } },
  },
  {
    .name = "build_dep_identity_ignores_profile",
    .project = "test/integration/fixtures/script/build_dep_static",
    .builds = { { "build" }, { "build", "-m", "release" } },
    .expect.store = { { "spum", 1 } },
  },
  {
    .name = "dual_membership_gets_two_store_entries",
    .project = "test/integration/fixtures/script/dual_ctx",
    .builds = { { "build" } },
    .expect.store = { { "gamma", 2 } },
  },
  {
    .name = "dual_membership_configures_once",
    .project = "test/integration/fixtures/script/dual_ctx",
    .builds = { { "build" } },
    .expect.configured = { { "gamma", 1 } },
  },
};

sp_test_each(script, identity, identity_test_t, identities) {
  return run_identity_test(t, *it);
}

static const command_test_t rejections [] = {
  { .name = "handle_null", .project = "test/integration/fixtures/script/handle_null", .args = { "build" } },
  { .name = "handle_kind", .project = "test/integration/fixtures/script/handle_kind", .args = { "build" } },
  { .name = "name_separator_node", .project = "test/integration/fixtures/script/name_separator_node", .args = { "build" } },
  { .name = "node_fn_without_build_script", .project = "test/integration/fixtures/script/node_fn_without_build_script", .args = { "build" } },
  { .name = "link_no_outputs", .project = "test/integration/fixtures/script/link_no_outputs", .args = { "build" } },
  { .name = "node_output_root", .project = "test/integration/fixtures/script/node_output_root", .args = { "build" } },
  { .name = "node_output_source", .project = "test/integration/fixtures/script/node_output_source", .args = { "build" } },
  { .name = "node_output_include", .project = "test/integration/fixtures/script/node_output_include", .args = { "build" } },
  { .name = "node_output_unnamed", .project = "test/integration/fixtures/script/node_output_unnamed", .args = { "build" } },
  { .name = "node_output_unmounted", .project = "test/integration/fixtures/script/node_output_unmounted", .args = { "build" } },
  { .name = "node_output_escape", .project = "test/integration/fixtures/script/node_output_escape", .args = { "build" } },
  { .name = "io_write_unmounted", .project = "test/integration/fixtures/script/io_write_unmounted", .args = { "build" } },
  { .name = "io_write_not_normal", .project = "test/integration/fixtures/script/io_write_not_normal", .args = { "build" } },
  { .name = "io_write_directory", .project = "test/integration/fixtures/script/io_write_directory", .args = { "build" } },
  { .name = "io_write_configure", .project = "test/integration/fixtures/script/io_write_configure", .args = { "build" } },
  { .name = "fmt_missing_arg", .project = "test/integration/fixtures/script/fmt_missing_arg", .args = { "build" } },
  { .name = "fmt_bad_placeholder", .project = "test/integration/fixtures/script/fmt_bad_placeholder", .args = { "build" } },
};

sp_test_each(script, rejection, command_test_t, rejections) {
  return run_command_test(t, *it);
}

static const command_test_t failures [] = {
  { .name = "nested_output", .project = "test/integration/fixtures/script/nested_output", .args = { "build" }, .expect = { .rc = 1, .err = SPN_ERR_DAG_NESTED_OUTPUT } },
  { .name = "configure_error", .project = "test/integration/fixtures/script/configure_error", .args = { "build" }, .expect = { .rc = 1, .err = SPN_ERR_WASM_SCRIPT_ERROR } },
  { .name = "propagated_error", .project = "test/integration/fixtures/script/propagated_error", .args = { "build" }, .expect = { .rc = 1, .err = SPN_ERR_NODE_NO_OUTPUTS } },
  { .name = "missing_node_fn", .project = "test/integration/fixtures/script/missing_node_fn", .args = { "build" }, .expect = { .rc = 1, .err = SPN_ERR_WASM_EXPORT_NOT_FOUND } },
};

sp_test_each(script, failure, command_test_t, failures) {
  return run_command_test(t, *it);
}
