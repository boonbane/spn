#include "compiler.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  test_profile_t profile;
  const c8* stdinc [2];
  render_expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "stdinc_follows_the_launcher",
    .driver = SPN_CC_DRIVER_ZIG,
    .profile = {
      .target = { SPN_ARCH_X64, SPN_OS_LINUX, SPN_ABI_MUSL },
    },
    .stdinc = { "-nostdinc", "-isystem/A" },
    .expect = {
      .command = "cc",
      .args = { "--target=x86_64-linux-musl", "-nostdinc", "-isystem/A", "-std=c99", "-c", "-Werror=return-type", "main.c" },
    },
  },
};

sp_test_each(render_source, render, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_cc_t toolchain = test_toolchain(it->driver);
  spn_profile_info_t profile = sp_zero;
  sp_try(test_profile(t, it->driver, it->profile, &profile));

  spn_cc_compile_t compile = sp_zero;
  spn_cc_compile_files_t files = { .source = test_arg_path("main.c") };
  spn_cc_compile_plan_t plan = si_cc_render_compile(mem, &toolchain, &profile, SPN_LANG_C, &compile);
  si_da(sp_str_t) stdinc = sp_zero;
  sp_carr_for(it->stdinc, at) {
    test_push_str(mem, &stdinc, it->stdinc[at]);
  }
  spn_invocation_t invocation = si_cc_render_compile_source(mem, &plan, &files, stdinc);
  return expect_invocation(t, &invocation, it->expect);
}
