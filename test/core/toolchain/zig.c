#include "toolchain.h"

typedef struct {
  const c8* name;
  spn_path_t root;
  spn_path_t expect;
} zig_cache_test_t;

static const zig_cache_test_t tests [] = {
  {
    .name = "artifact",
    .root = { .root = SPN_PATH_ROOT_TOOLCHAIN, .sub = sp_str_lit("A") },
    .expect = { .root = SPN_PATH_ROOT_TOOLCHAIN, .sub = sp_str_lit("A.cache") },
  },
  {
    .name = "absolute",
    .root = { .sub = sp_str_lit("/S/T/A") },
    .expect = { .sub = sp_str_lit("/S/T/A.cache") },
  },
};

sp_test_each(toolchain_zig, cache_dir, zig_cache_test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_t cache = spn_toolchain_zig_cache_dir(mem, it->root);
  sp_expect_eq(t, cache.root, it->expect.root);
  sp_expect_str_eq(t, cache.sub, it->expect.sub);
  return SP_OK;
}
