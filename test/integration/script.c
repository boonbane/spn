#include "harness.h"

static const c8* project(sp_test_t* t, const c8* name) {
  return sp_str_to_cstr(sp_test_arena(t), sp_fmt(sp_test_arena(t), "test/integration/fixtures/script/{}", sp_fmt_cstr(name)).value);
}

typedef struct {
  const c8* name;
  const c8* work [SPN_TEST_COMMAND_MAX_PATHS];
} graph_t;

// Fixtures self-verify with _Static_assert on what their nodes generated, so a
// row only names outputs nothing includes
static const graph_t graphs [] = {
  { .name = "basic_node" },
  { .name = "tree_output" },
  { .name = "diamond_deps" },
  { .name = "multi_output" },
  { .name = "node_linking" },
  { .name = "orphan_outputs", .work = { "O/O.h" } },
};

sp_test_each(script, graph, graph_t, graphs) {
  command_test_t test = {
    .project = project(t, it->name),
    .args = { "build" },
  };
  sp_carr_for(it->work, i) {
    if (!it->work[i]) {
      break;
    }
    test.expect.exists[i] = work_file(it->work[i]);
  }
  return run_command_test(t, test);
}

sp_test(script, chained_nodes) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/script/chained_nodes",
    .copy = { "a.txt", "a.change.txt" },
    .first = {
      .args = { "build" },
      .expect.files = { { .file = work_file("C/B.txt"), .content = "2" } },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = sp_str_lit("a.change.txt"), .to = sp_str_lit("a.txt") },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "B" } },
            .files = { { .file = work_file("C/B.txt"), .content = "6" } },
          },
        },
      },
    },
  });
}

sp_test(script, shared_script) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/shared_script",
    .args = { "build" },
    .expect.files = { { .file = work_file("S/V.txt"), .content = "1" } },
  });
}

sp_test(script, script_edit) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/script/script_edit",
    .first = {
      .args = { "build" },
      .expect.files = { { .file = work_file("E/V.txt"), .content = "1" } },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = sp_str_lit("tools/build.change.c"), .to = sp_str_lit("tools/build.c") },
        },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .key = "tag", .value = "gen" } },
            .files = { { .file = work_file("E/V.txt"), .content = "2" } },
          },
        },
      },
    },
  });
}

sp_test(script, tree_output_cached) {
  return run_rebuild_test(t, (rebuild_test_t) {
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
  });
}

sp_test(script, publish_replay) {
  return run_rebuild_test(t, (rebuild_test_t) {
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
        .change.remove_dirs = { sp_str_lit("build") },
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
  });
}

sp_test(script, tree_output_rerun_drops_file) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/script/tree_output_drop",
    .copy = { "H" },
    .first = {
      .args = { "build" },
      .expect.exists = { work_file("M/gen/G/a.h"), work_file("M/gen/G/d.h") },
    },
    .rebuilds = {
      {
        .change.remove_files = { sp_str_lit("H/d.h") },
        .command = {
          .args = { "build" },
          .expect = {
            .exists = { work_file("M/gen/G/a.h") },
            .missing = { work_file("M/gen/G/d.h") },
          },
        },
      },
    },
  });
}

typedef struct {
  const c8* name;
  const c8* copy [4];
  spn_err_t err;
} failure_t;

static const failure_t failures [] = {
  { .name = "node_output_root", .err = SPN_ERR_WASM_MODULE_CALL_FAILED },
  { .name = "node_output_source", .err = SPN_ERR_WASM_MODULE_CALL_FAILED },
  { .name = "node_output_include", .err = SPN_ERR_WASM_MODULE_CALL_FAILED },
  { .name = "node_output_unnamed", .err = SPN_ERR_WASM_MODULE_CALL_FAILED },
  { .name = "relative_path", .err = SPN_ERR_WASM_MODULE_CALL_FAILED },
  { .name = "foreign_path", .err = SPN_ERR_WASM_MODULE_CALL_FAILED },
  { .name = "node_output_absolute", .err = SPN_ERR_WASM_MODULE_CALL_FAILED },
  { .name = "name_separator_node", .err = SPN_ERR_WASM_MODULE_CALL_FAILED },
  { .name = "nested_output", .err = SPN_ERR_DAG_NESTED_OUTPUT },
  { .name = "configure_missing_source", .err = SPN_ERR_CONFIGURE_SOURCE_MISSING },
  { .name = "configure_dead_glob", .err = SPN_ERR_CONFIGURE_SOURCE_GLOB },
  { .name = "configure_error", .err = SPN_ERR_WASM_SCRIPT_ERROR },
  { .name = "missing_node_fn", .err = SPN_ERR_WASM_EXPORT_NOT_FOUND },
  { .name = "node_fn_without_build_script", .err = SPN_ERR_NODE_NO_BUILD_SCRIPT },
  { .name = "add_define_path_outside", .err = SPN_ERR_WASM_MODULE_CALL_FAILED },
};

sp_test_each(script, failure, failure_t, failures) {
  command_test_t test = {
    .project = project(t, it->name),
    .args = { "build" },
    .expect = { .rc = 1, .err = it->err },
  };
  sp_carr_for(it->copy, i) {
    test.copy[i] = it->copy[i];
  }
  return run_command_test(t, test);
}

sp_test(script, node_output_bin) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/node_output_bin",
    .args = { "build" },
    .expect = {
      .exists = { pkg_store_file("B", "bin/R.txt") },
    },
  });
}

sp_test(script, cross_package) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/cross_package",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked.name = "core/spum" },
      { .kind = ACTION_RUN_BIN, .bin.name = "cross_package" },
    },
  });
}

sp_test(script, object_lib) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/object_lib",
    .copy = { "packages/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      // object libs publish their objects to lib/, preserving source-relative paths
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_store_file("spum", "lib/manifest/rt/extra.c.o") },
      // an unlinked archive still builds and installs
      { .kind = ACTION_VERIFY_EXISTS, .exists = pkg_static_lib("spum", "blob") },
      { .kind = ACTION_RUN_BIN, .bin.name = "object_lib" },
    },
  });
}

sp_test(script, configure_table) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/configure_table",
    .copy = { "include" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin.name = "configure_table" },
    },
  });
}

sp_test(script, configure_glob) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/configure_glob",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EVENT_COUNT, .verify_event_count = { .event = SPN_EVENT_USER_LOG, .key = "message", .value = "G", .count = 1 } },
    },
  });
}

sp_test(script, wrong_handle) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/wrong_handle",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin.name = "wrong_handle" },
    },
  });
}

sp_test(script, build_script) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/script/build_script",
    .copy = { "include", "vendor" },
    .first = {
      .args = { "build" },
      .expect = {
        .bin.name = "build_script",
        .cc = {
          { .args = { "tools/configure.c" } },
          { .args = { "tools/build.c" } },
        },
        .exists = { work_file("build_script/version.h") },
      },
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
  });
}

sp_test(script, build_deps) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/build_deps",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked.name = "core/spum" },
      { .kind = ACTION_VERIFY_DIR_COUNT, .verify_dir_count = { .dir = ".home/storage/cache/store/core/spum", .count = 1 } },
      { .kind = ACTION_VERIFY_EVENT_COUNT, .verify_event_count = { .event = SPN_EVENT_USER_LOG, .key = "message", .value = "spum configure", .count = 0 } },
      { .kind = ACTION_RUN_BIN, .bin.name = "build_deps" },
    },
  });
}

sp_test(script, build_deps_replay) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/script/build_deps",
    .first = {
      .args = { "build" },
      .expect.exists = { exe("build_deps") },
    },
    .rebuilds = {
      {
        .change.remove_dirs = { sp_str_lit("build") },
        .command = {
          .args = { "build" },
          .expect = {
            .events = { { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true } },
            .exists = { exe("build_deps") },
          },
        },
      },
    },
  });
}

sp_test(script, dual_ctx) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/dual_ctx",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EVENT_COUNT, .verify_event_count = { .event = SPN_EVENT_USER_LOG, .key = "message", .value = "gamma configure", .count = 1 } },
      { .kind = ACTION_VERIFY_DIR_COUNT, .verify_dir_count = { .dir = ".home/storage/cache/store/core/gamma", .count = 2 } },
      { .kind = ACTION_RUN_BIN, .bin.name = "dual_ctx" },
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_EVENT_COUNT, .verify_event_count = { .event = SPN_EVENT_USER_LOG, .key = "message", .value = "gamma configure", .count = 1 } },
    },
  });
}

sp_test(script, program_name_context) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/module_name_collision",
    .args = { "build" },
    .expect.bin.name = "configure",
  });
}

sp_test(script, build_dep_closure) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/build_dep_closure",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked.name = "core/alpha" },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked.name = "core/beta" },
      { .kind = ACTION_VERIFY_DIR_COUNT, .verify_dir_count = { .dir = ".home/storage/cache/store/core/alpha", .count = 1 } },
      { .kind = ACTION_VERIFY_DIR_COUNT, .verify_dir_count = { .dir = ".home/storage/cache/store/core/beta", .count = 1 } },
      { .kind = ACTION_RUN_BIN, .bin.name = "build_dep_closure" },
    },
  });
}

sp_test(script, build_dep_static) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/build_dep_static",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_VERIFY_PKG_LOCKED, .verify_locked.name = "core/spum" },
      { .kind = ACTION_VERIFY_DIR_COUNT, .verify_dir_count = { .dir = ".home/storage/cache/store/core/spum", .count = 1 } },
    },
  });
}

sp_test(script, build_dep_profiles) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/build_dep_static",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_CLI, .cli = { .cmd = "build", .args = { "-m", "release" } } },
      { .kind = ACTION_VERIFY_DIR_COUNT, .verify_dir_count = { .dir = ".home/storage/cache/store/core/spum", .count = 1 } },
      { .kind = ACTION_RUN_BIN, .bin.name = "build_dep_static" },
    },
  });
}

sp_test(script, add_define) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/add_define",
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
    },
  });
}

sp_test(script, add_define_path) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/add_define_path",
    .args = { "build" },
    .expect.cc = { { .args = { "-DG=\"build/debug/.spn/A/gen\"", "/DG=\"build/debug/.spn/A/gen\"" } } },
  });
}

sp_test(script, add_include) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/add_include",
    .copy = { "include/*" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin.name = "main" },
    },
  });
}

sp_test(script, add_system_dep) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/add_system_dep",
    .args = { "build" },
    .expect.exists = { exe("main") },
  });
}

sp_test(script, embed) {
  return run_test(t, (test_t) {
    .project = "test/integration/fixtures/script/embed",
    .copy = { "hello.txt" },
    .actions = {
      { .kind = ACTION_RUN_CLI, .cli.cmd = "build" },
      { .kind = ACTION_RUN_BIN, .bin = { .name = "main", .rc = 0 } },
    },
  });
}

sp_test(script, embed_dir) {
  return run_rebuild_test(t, (rebuild_test_t) {
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
        .change.moves = {
          { .from = sp_str_lit("H/a.change.h"), .to = sp_str_lit("H/a.h") },
        },
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
  });
}

sp_test(script, embed_cross_linux_aarch64) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/embed",
    .copy = { "hello.txt" },
    .when.target = "aarch64-linux-musl",
    .args = { "build", "--target", "aarch64-linux-musl" },
    .expect = { .exists = { target_exe("main", "aarch64-linux-musl") } },
  });
}

sp_test(script, embed_cross_windows_aarch64) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/embed",
    .copy = { "hello.txt" },
    .when.target = "aarch64-windows-gnu",
    .args = { "build", "--target", "aarch64-windows-gnu" },
    .expect = { .exists = { target_exe("main", "aarch64-windows-gnu") } },
  });
}

sp_test(script, input_order) {
  return sp_test_skip(t, "pending: canonicalize declared node input/output order in DAG construction");

  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/script/input_order",
    .copy = { "a.txt", "b.txt", "c.txt", "inputs.txt", "inputs.reordered.txt" },
    .first = {
      .args = { "build" },
      .expect = {
        .events = { { .event = SPN_EVENT_SCRIPT_USER_FN } },
        .exists = { work_file("input_order/gen.h") },
      },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = sp_str_lit("inputs.reordered.txt"), .to = sp_str_lit("inputs.txt") },
        },
        .command = {
          .args = { "build" },
          .expect.events = { { .event = SPN_EVENT_SCRIPT_USER_FN, .absent = true } },
        },
      },
    },
  });
}

sp_test(script, generated_source) {
  return run_command_test(t, (command_test_t) {
    .project = "test/integration/fixtures/script/generated_source",
    .args = { "build" },
    .expect = {
      .exists = { exe("generated_source") },
    },
  });
}
