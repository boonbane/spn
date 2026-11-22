#include "compiler.h"

typedef struct {
  spn_err_t err;
  spn_cc_feature_t feature;
} expect_t;

typedef struct {
  const c8* name;
  spn_triple_t host;
  spn_triple_t target;
  spn_ld_family_t linker;
  spn_sdk_kind_t sdk;
  spn_cc_output_kind_t kind;
  const c8* framework;
  const c8* script;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "reactor_needs_wasm",
    .target = HOST_X64_LINUX,
    .kind = SPN_CC_OUTPUT_REACTOR,
    .expect = { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = SPN_CC_FEATURE_LINK_REACTOR },
  },
  {
    .name = "wasm_links_a_reactor",
    .target = TARGET_WASM,
    .kind = SPN_CC_OUTPUT_REACTOR,
  },
  {
    .name = "shared_lib_needs_a_loader",
    .target = TARGET_X64_BARE,
    .kind = SPN_CC_OUTPUT_SHARED_LIB,
    .expect = { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = SPN_CC_FEATURE_LINK_SHARED },
  },
  {
    .name = "loader_links_a_shared_lib",
    .target = HOST_X64_LINUX,
    .kind = SPN_CC_OUTPUT_SHARED_LIB,
  },
  {
    .name = "frameworks_need_an_sdk",
    .target = HOST_ARM_MACOS,
    .kind = SPN_CC_OUTPUT_EXE,
    .framework = "A",
    .expect = { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = SPN_CC_FEATURE_FRAMEWORKS },
  },
  {
    .name = "sdk_links_frameworks",
    .target = HOST_ARM_MACOS,
    .sdk = SPN_SDK_MACOS,
    .kind = SPN_CC_OUTPUT_EXE,
    .framework = "A",
  },
  {
    .name = "frameworks_off_macos_are_ignored",
    .target = HOST_X64_LINUX,
    .kind = SPN_CC_OUTPUT_EXE,
    .framework = "A",
  },
  {
    .name = "linker_script_needs_a_linker_that_takes_one",
    .target = HOST_ARM_MACOS,
    .linker = SPN_LD_FAMILY_LD64,
    .kind = SPN_CC_OUTPUT_EXE,
    .script = "A",
    .expect = { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = SPN_CC_FEATURE_LINKER_SCRIPT },
  },
  {
    .name = "gnu_linker_takes_a_script",
    .target = HOST_X64_LINUX,
    .linker = SPN_LD_FAMILY_GNU,
    .kind = SPN_CC_OUTPUT_EXE,
    .script = "A",
  },
  {
    .name = "msvc_linker_needs_windows_host",
    .host = HOST_X64_LINUX,
    .target = TARGET_WIN_MSVC,
    .linker = SPN_LD_FAMILY_MSVC,
    .kind = SPN_CC_OUTPUT_EXE,
    .expect = { .err = SPN_ERR_TOOLCHAIN_MSVC_LINKER_HOST },
  },
  {
    .name = "msvc_linker_runs_on_windows",
    .host = HOST_X64_WINDOWS,
    .target = TARGET_WIN_MSVC,
    .linker = SPN_LD_FAMILY_MSVC,
    .kind = SPN_CC_OUTPUT_EXE,
  },
  {
    .name = "lld_links_msvc_off_windows",
    .host = HOST_X64_LINUX,
    .target = TARGET_WIN_MSVC,
    .linker = SPN_LD_FAMILY_LLD,
    .kind = SPN_CC_OUTPUT_EXE,
  },
};

sp_test_each(validate_link, refusal, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_profile_info_t profile = {
    .target = it->target,
    .linker = it->linker,
    .sdk = { .kind = it->sdk },
  };
  spn_cc_link_t link = { .kind = it->kind };
  test_push_str(mem, &link.frameworks, it->framework);
  test_push_path(mem, &link.scripts, it->script);

  spn_cc_link_refusal_t refusal = spn_cc_validate_link(it->host, &profile, &link);
  sp_expect_eq(t, refusal.err, it->expect.err);
  sp_expect_eq(t, refusal.feature, it->expect.feature);
  return SP_OK;
}
