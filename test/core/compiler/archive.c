#include "compiler.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  const c8* cache;
  test_profile_t profile;
  const c8* rsp;
  render_expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "gnu",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU } },
    .expect = {
      .command = "ar",
      .args = { "rcs", "main.a", "main.o" },
    },
  },
  {
    .name = "gnu_objects_from_rsp",
    .driver = SPN_CC_DRIVER_GCC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU } },
    .rsp = "A.rsp",
    .expect = {
      .command = "ar",
      .args = { "rcs", "main.a", "@A.rsp" },
    },
  },
  {
    .name = "zig_cache_env",
    .driver = SPN_CC_DRIVER_ZIG,
    .cache = "/C",
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_GNU } },
    .expect = {
      .command = "ar",
      .args = { "rcs", "main.a", "main.o" },
      .env = { "ZIG_GLOBAL_CACHE_DIR=/C", "ZIG_LOCAL_CACHE_DIR=/C" },
    },
  },
  {
    .name = "msvc",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC } },
    .expect = {
      .command = "ar",
      .args = { "/nologo", "/OUT:main.a", "main.o" },
    },
  },
  {
    .name = "msvc_objects_from_rsp",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC } },
    .rsp = "A.rsp",
    .expect = {
      .command = "ar",
      .args = { "/nologo", "/OUT:main.a", "@A.rsp" },
    },
  },
  {
    .name = "msvc_sdk_bin_selects_lib",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC }, .sdk = "/X", .msvc.bin = "/X/bin" },
    .expect = {
      .command = "/X/bin/lib.exe",
      .args = { "/nologo", "/OUT:main.a", "main.o" },
    },
  },
  {
    .name = "msvc_sdk_tools_join_path",
    .driver = SPN_CC_DRIVER_MSVC,
    .profile = { .target = { SPN_ARCH_X64, SPN_OS_WINDOWS, SPN_ABI_MSVC }, .sdk = "/X", .msvc.tools = "/T" },
    .expect = {
      .command = "ar",
      .args = { "/nologo", "/OUT:main.a", "main.o" },
      .path = { "/T" },
    },
  },
};

sp_test_each(render_archive, render, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(it->driver);
  toolchain.cache = test_arg_path(it->cache);
  spn_profile_info_t profile = sp_zero;
  sp_try(test_profile(t, it->driver, it->profile, &profile));

  spn_cc_archive_files_t files = {
    .output = test_arg_path("main.a"),
    .rsp = test_arg_path(it->rsp),
  };
  if (!it->rsp) {
    test_push_path(mem, &files.objects, "main.o");
  }

  spn_cc_archive_plan_t plan = si_cc_render_archive(mem, &toolchain, &profile);
  spn_invocation_t invocation = spn_cc_render_archive_command(mem, &plan, &files);
  return expect_invocation(t, &invocation, it->expect);
}
