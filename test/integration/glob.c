#include "harness.h"

static const rebuild_test_t rebuilds [] = {
  {
    .name = "source_glob_recurses_and_sees_new_dirs",
    .project = "test/integration/fixtures/glob/source",
    .copy = { "src", "two.c", "main.two.c" },
    .first = {
      .args = { "build" },
      .expect.exists = { exe("main") },
    },
    .rebuilds = {
      {
        .change.moves = {
          { .from = "two.c", .to = "src/b/two.c" },
          { .from = "main.two.c", .to = "main.c" },
        },
        .command = {
          .args = { "build" },
          .expect.exists = { exe("main") },
        },
      },
    },
    .watches = { { exe("main"), REBUILD_MTIME_CHANGED } },
  },
};

sp_test_each(glob, rebuild, rebuild_test_t, rebuilds) {
  return run_rebuild_test(t, *it);
}
