#include "paths/paths_test.h"

typedef struct {
  spn_path_root_t root;
  const c8* sub;
} layout_path_t;

typedef struct {
  const c8* name;
  layout_path_t path;
  bool pinned;
} layout_test_t;

static const layout_test_t layout_tests [] = {
  {
    .name = "store",
    .path = { SPN_PATH_ROOT_STORAGE, "cache/store/N/P" }
  },
  {
    .name = "build",
    .path = { SPN_PATH_ROOT_STORAGE, "cache/build/N/P" }
  },
  {
    .name = "checkouts_are_pinned",
    .path = { SPN_PATH_ROOT_STORAGE, "cache/source/checkouts/K/H" },
    .pinned = true
  },
  {
    .name = "git_db",
    .path = { SPN_PATH_ROOT_STORAGE, "cache/source/db/K" }
  },
  {
    .name = "dag",
    .path = { SPN_PATH_ROOT_STORAGE, "cache/dag/store/H" }
  },
  {
    .name = "index",
    .path = { SPN_PATH_ROOT_STORAGE, "index/K/P" }
  },
  {
    .name = "runtime",
    .path = { SPN_PATH_ROOT_STORAGE, "runtime/include/H" }
  },
  {
    .name = "toolchain_store_is_pinned",
    .path = { SPN_PATH_ROOT_TOOLCHAIN, "store/D/bin/cc" },
    .pinned = true
  },
  {
    .name = "toolchain_external_is_not_pinned",
    .path = { SPN_PATH_ROOT_TOOLCHAIN, "external/zig/cache/I" }
  },
  {
    .name = "same_sub_under_another_root_is_not_pinned",
    .path = { SPN_PATH_ROOT_PROJECT, "cache/source/checkouts/K/H" }
  },
};

sp_test_each(paths_layout, pinned, layout_test_t, layout_tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = { .pinned = spn_layout_pinned(mem) };
  spn_path_t path = { .root = it->path.root, .sub = sp_str_view(it->path.sub) };

  sp_expect_eq(t, (u32)spn_path_pinned(&roots, path), (u32)it->pinned);
  return SP_OK;
}

sp_test(paths_layout, dirs_are_disjoint) {
  for (u32 it = SPN_DIR_ID_NONE + 1; it < SPN_DIR_ID_COUNT; it++) {
    for (u32 jt = SPN_DIR_ID_NONE + 1; jt < SPN_DIR_ID_COUNT; jt++) {
      if (it == jt) {
        continue;
      }
      sp_expect(t, !spn_path_within(spn_path_from_id((spn_dir_id_t)it), spn_path_from_id((spn_dir_id_t)jt)).within);
    }
  }
  return SP_OK;
}
