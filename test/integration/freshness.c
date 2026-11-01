#include "harness.h"

sp_test(freshness, noop) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/bin",
    .first = {
      .args = { "build" },
      .expect = {
        .events = { { .event = SPN_EVENT_TARGET_BUILD_PASSED } },
        .exists = { exe("main") },
      },
    },
    .rebuilds = {
      {
        .command = {
          .args = { "build" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
            { .event = SPN_EVENT_LINK_PASSED, .absent = true },
          },
        },
      },
    },
  });
}

sp_test(freshness, source_change) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/bin",
    .copy = { "main.change.c" },
    .first = {
      .args = { "build" },
      .expect.exists = { exe("main") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "main.change.c", .to = "main.c" },
        },
        .command = {
          .args = { "build" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED },
            { .event = SPN_EVENT_LINK_PASSED },
          },
        },
      },
    },
  });
}

sp_test(freshness, touch_without_change) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/bin",
    .copy = { "main.same.c" },
    .first = {
      .args = { "build" },
      .expect.exists = { exe("main") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "main.same.c", .to = "main.c" },
        },
        .command = {
          .args = { "build" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
            { .event = SPN_EVENT_LINK_PASSED, .absent = true },
          },
        },
      },
    },
  });
}

sp_test(freshness, staged_lib_noop) {
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
          .args = { "build" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
            { .event = SPN_EVENT_LINK_PASSED, .absent = true },
          },
        },
      },
    },
  });
}

sp_test(freshness, staged_lib_change) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/shared",
    .copy = { "packages/*", "t.c" },
    .first = {
      .args = { "build" },
      .expect.exists = { exe("main"), staged_lib("B") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "packages/B/b.change.c", .to = "packages/B/b.c" },
        },
        .command = {
          .args = { "build" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "B" },
            { .event = SPN_EVENT_LINK_PASSED, .key = "subject.target", .value = "B" },
          },
        },
      },
    },
  });
}

sp_test(freshness, dep_source_change) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/dep",
    .copy = { "packages/*" },
    .first = {
      .args = { "build", "-p", "debug" },
      .expect.exists = { pkg_static_lib("spum", "spum"), pkg_store_file("test", "bin/main") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "packages/spum/spum.change.c", .to = "packages/spum/spum.c" },
        },
        .command = {
          .args = { "build", "-p", "debug" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "spum" },
            { .event = SPN_EVENT_LINK_PASSED, .key = "subject.target", .value = "spum" },
            { .event = SPN_EVENT_LINK_PASSED, .key = "subject.target", .value = "main" },
          },
        },
      },
      {
        .command = {
          .args = { "build", "-p", "debug" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
            { .event = SPN_EVENT_LINK_PASSED, .absent = true },
          },
        },
      },
    },
  });
}

sp_test(freshness, dep_header_inert) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/dep",
    .copy = { "packages/*" },
    .when.deterministic = true,
    .first = {
      .args = { "build", "-p", "debug" },
      .expect.exists = { pkg_store_file("test", "bin/main") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "packages/spum/spum.inert.h", .to = "packages/spum/spum.h" },
        },
        .command = {
          .args = { "build", "-p", "debug" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "main" },
            { .event = SPN_EVENT_LINK_PASSED, .absent = true },
          },
        },
      },
    },
  });
}

sp_test(freshness, node_header_inert) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/node",
    .copy = { "other.c", "version.txt", "version.change.txt" },
    .first = {
      .args = { "build" },
      .expect.exists = { exe("M"), exe("O") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "version.change.txt", .to = "version.txt" },
        },
        .command = {
          .args = { "build" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "M" },
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "O", .absent = true },
          },
        },
      },
    },
  });
}

sp_test(freshness, dep_unincluded_header_change) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/dep",
    .copy = { "packages/*" },
    .first = {
      .args = { "build", "-p", "debug" },
      .expect.exists = { pkg_store_file("spum", "include/extra.h"), pkg_store_file("test", "bin/main") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "packages/spum/extra.change.h", .to = "packages/spum/extra.h" },
        },
        .command = {
          .args = { "build", "-p", "debug" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
            { .event = SPN_EVENT_LINK_PASSED, .absent = true },
          },
        },
      },
    },
  });
}

sp_test(freshness, dep_header_change) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/dep",
    .copy = { "packages/*", "main.code.c" },
    .first = {
      .args = { "build", "-p", "debug" },
      .expect.exists = { pkg_store_file("test", "bin/main") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "main.code.c", .to = "main.c" },
          { .from = "packages/spum/spum.seven.h", .to = "packages/spum/spum.h" },
        },
        .command = {
          .args = { "build", "-p", "debug" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "main" },
            { .event = SPN_EVENT_LINK_PASSED, .key = "subject.target", .value = "main" },
          },
        },
      },
      {
        .change.moves = {
          { .from = "packages/spum/spum.eight.h", .to = "packages/spum/spum.h" },
        },
        .command = {
          .args = { "build", "-p", "debug" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .key = "subject.target", .value = "main" },
            { .event = SPN_EVENT_LINK_PASSED, .key = "subject.target", .value = "main" },
          },
        },
      },
    },
  });
}

sp_test(freshness, output_deleted) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/bin",
    .first = {
      .args = { "build" },
      .expect.exists = { exe("main") },
    },
    .rebuilds = {
      {
        .change.remove_files = { exe("main") },
        .command = {
          .args = { "build" },
          .expect = {
            .events = {
              { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
              { .event = SPN_EVENT_LINK_PASSED, .absent = true },
            },
            .exists = { exe("main") },
          },
        },
      },
    },
  });
}

sp_test(freshness, linker_script_change) {
  return run_rebuild_test(t, (rebuild_test_t) {
    .project = "test/integration/fixtures/freshness/linker_script",
    .copy = { "main.ld", "main.change.ld" },
    .when.target = SPN_TEST_ARCH "-freestanding-none",
    .first = {
      .args = { "build", "--target", SPN_TEST_ARCH "-freestanding-none" },
      .expect.exists = { target_exe("main", SPN_TEST_ARCH "-freestanding-none") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "main.change.ld", .to = "main.ld" },
        },
        .command = {
          .args = { "build", "--target", SPN_TEST_ARCH "-freestanding-none" },
          .expect.events = {
            { .event = SPN_EVENT_TARGET_BUILD_PASSED, .absent = true },
            { .event = SPN_EVENT_LINK_PASSED },
          },
        },
      },
    },
  });
}
