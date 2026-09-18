#include "compiler.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t compiler;
  spn_ar_driver_t archiver;
  test_profile_t profile;
  render_expect_t expect;
} archive_test_t;

static const archive_test_t tests [] = {
  {
    .name = "gnu_archiver",
    .compiler = SPN_CC_DRIVER_MSVC,
    .archiver = SPN_AR_DRIVER_GNU,
    .profile = { .arch = SPN_ARCH_X64, .os = SPN_OS_LINUX, .abi = SPN_ABI_GNU },
    .expect = {
      .command = "ar",
      .args = { "rcs", "libmain.a", "main.o" },
    },
  },
  {
    .name = "msvc_archiver",
    .compiler = SPN_CC_DRIVER_GCC,
    .archiver = SPN_AR_DRIVER_MSVC,
    .profile = { .arch = SPN_ARCH_X64, .os = SPN_OS_LINUX, .abi = SPN_ABI_GNU },
    .expect = {
      .command = "ar",
      .args = { "/nologo", "/OUT:libmain.a", "main.o" },
    },
  },
  {
    .name = "msvc_sdk_bin_selects_lib",
    .compiler = SPN_CC_DRIVER_MSVC,
    .archiver = SPN_AR_DRIVER_MSVC,
    .profile = { .arch = SPN_ARCH_ARM64, .os = SPN_OS_WINDOWS, .abi = SPN_ABI_MSVC, .sdk = "/X", .bin = "/X/bin/arm64" },
    .expect = {
      .command = "/X/bin/arm64/lib.exe",
      .args = { "/nologo", "/OUT:libmain.a", "main.o" },
    },
  },
};

sp_test_each(render_archive, render, archive_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_toolchain_t toolchain = test_toolchain(it->compiler);
  toolchain.archiver_driver = it->archiver;
  spn_profile_info_t profile = test_profile(it->profile);
  spn_cc_archive_files_t files = {
    .output = test_arg_path("libmain.a"),
  };
  sp_da_init(mem, files.objects);
  sp_da_push(files.objects, spn_arg_path(test_arg_path("main.o")));

  spn_invocation_t invocation = spn_cc_render_archive(mem, &toolchain, &profile, &files);
  return expect_args(t, &invocation, it->expect);
}
