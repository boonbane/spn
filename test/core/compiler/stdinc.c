#include "compiler.h"

typedef struct {
  spn_cc_stdinc_kind_t kind;
  render_expect_t query;
} expect_t;

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  const c8* cache;
  test_profile_t profile;
  const c8* fragment;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "gcc_sysroot_moves_the_search",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = {
      .target = { SPN_ARCH_ARM64, SPN_OS_LINUX, SPN_ABI_GNU },
      .sdk = "/S",
    },
    .expect = {
      .kind = SPN_CC_STDINC_SEARCH_LIST,
      .query = {
        .command = "cc",
        .args = { "--sysroot=/S", "-E", "-v", "-x", "c", "-" },
      },
    },
  },
  {
    .name = "zig_writes_a_fragment",
    .driver = SPN_CC_DRIVER_ZIG,
    .cache = "/C",
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_MUSL },
    },
    .fragment = "F",
    .expect = {
      .kind = SPN_CC_STDINC_FRAGMENT,
      .query = {
        .command = "cc",
        .args = { "--target=x86_64-linux-musl", "-E", "-v", "-x", "c", "-", "-MJ", "F" },
        .env = { "ZIG_GLOBAL_CACHE_DIR=/C", "ZIG_LOCAL_CACHE_DIR=/C" },
      },
    },
  },
};

sp_test_each(render_stdinc, render, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(it->driver);
  toolchain.cache = test_arg_path(it->cache);
  spn_profile_info_t profile = sp_zero;
  sp_try(test_profile(t, it->driver, it->profile, &profile));

  spn_cc_stdinc_plan_t plan = si_cc_render_stdinc(mem, &toolchain, &profile, SPN_LANG_C);
  sp_must_eq(t, (u32)it->expect.kind, (u32)plan.kind);

  spn_cc_stdinc_files_t files = { .fragment = test_arg_path(it->fragment) };
  spn_invocation_t query = si_cc_render_stdinc_command(mem, &plan.query, &files);
  return expect_invocation(t, &query, it->expect.query);
}
