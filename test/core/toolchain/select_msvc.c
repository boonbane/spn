#include "toolchain.h"

#define max_installed 2

typedef struct {
  const c8* version;
  const c8* generation;
} install_t;

typedef struct {
  spn_err_t err;
  fixture_sdk_expect_t sdk;
  install_t installed [max_installed];
} expect_t;

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  test_arg_t sdk;
  test_arg_t fallback;
  fixture_sdks_t sdks;
  const c8* toolset;
  const c8* kit;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "request_rebinds_a_detected_row",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44" }, { { "/Y" }, SPN_ARCH_X64, "14.29" } } },
    .toolset = "v142",
    .expect = { .sdk = { SPN_SDK_MSVC, { "/Y" } } },
  },
  {
    .name = "explicit_sdk_ignores_the_request",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdk = { .path = "/S" },
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44" } } },
    .toolset = "v141",
    .expect = { .sdk = { SPN_SDK_MSVC, { "/S" } } },
  },
  {
    .name = "unmatched_toolset_lists_the_installed",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44" } } },
    .toolset = "v141",
    .expect = { .err = SPN_ERR_MSVC_TOOLSET, .installed = { { "14.44", "v143" } } },
  },
  {
    .name = "unmatched_sdk_lists_the_installed",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64 } } },
    .kit = "11",
    .expect = { .err = SPN_ERR_MSVC_SDK, .installed = { { "10.0" } } },
  },
  {
    .name = "msvc_driver_takes_a_toolset_with_a_compiler",
    .driver = SPN_CC_DRIVER_MSVC,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .bare = true }, { { "/Y" }, SPN_ARCH_X64, "14.29" } } },
    .expect = { .sdk = { SPN_SDK_MSVC, { "/Y" } } },
  },
  {
    .name = "other_drivers_take_a_bare_toolset",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .bare = true } } },
    .expect = { .sdk = { SPN_SDK_MSVC, { "/X" } } },
  },
  {
    .name = "msvc_driver_without_a_compiler_is_unserved",
    .driver = SPN_CC_DRIVER_MSVC,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .bare = true } } },
    .expect = { .err = SPN_ERR_TOOLCHAIN_SDK_MSVC },
  },
  {
    .name = "auto_does_not_fall_back_past_an_unmatched_request",
    .driver = SPN_CC_DRIVER_CLANG,
    .fallback = { .path = "/S" },
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44" } } },
    .toolset = "v141",
    .expect = { .err = SPN_ERR_MSVC_TOOLSET },
  },
};

static spn_toolchain_decl_t toolchain(sp_mem_t mem, const c8* name, spn_cc_driver_t driver, test_arg_t sdk) {
  spn_toolchain_decl_t decl = fixture_local_toolchain(name, (fixture_launcher_t) { .name = "cc" });
  decl.driver = driver;
  decl.host_row = false;
  decl.targets = sp_da_new(mem, spn_toolchain_target_t);
  sp_da_push(decl.targets, fixture_target((fixture_target_t) { .triple = TARGET_WIN_MSVC, .sdk = sdk }));
  return decl;
}

sp_test_each(select_msvc, pin, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_toolchain_catalog_t catalog = sp_zero;
  spn_toolchain_catalog_init(&catalog, (spn_triple_t) HOST_X64_LINUX, fixture_sdks(mem, it->sdks), mem);
  spn_toolchain_catalog_add(&catalog, toolchain(mem, "A", it->driver, it->sdk));
  if (it->fallback.path) {
    spn_toolchain_catalog_add(&catalog, toolchain(mem, "B", it->driver, it->fallback));
  }

  spn_toolchain_candidate_t candidate = { .triple = TARGET_WIN_MSVC };
  sp_must_eq(t, (u32)SPN_LINKING_REFUSAL_NONE, (u32)si_ld_linkings(candidate.triple, sp_zero_s(spn_linking_t), SPN_LIB_KIND_NONE, &candidate.linkings));
  spn_toolchain_query_t query = {
    .toolchain = it->fallback.path ? (spn_toolchain_ref_t) { .kind = SPN_TOOLCHAIN_REF_AUTO } : spn_toolchain_ref_from_str(sp_str_lit("A")),
    .target = TARGET_WIN_MSVC,
    .candidates = { .items = { candidate }, .count = 1 },
    .msvc = { .toolset = sp_cstr_as_str(it->toolset), .sdk = sp_cstr_as_str(it->kit) },
  };
  spn_toolchain_selection_t selection = sp_zero;
  sp_must_eq(t, (u32)it->expect.err, (u32)si_toolchain_select(&catalog, query, &selection));
  if (!it->expect.err) {
    return fixture_check_sdk(t, selection.row.sdk, it->expect.sdk);
  }

  sp_da(spn_event_t) errs = spn_test_drain_errs(mem);
  sp_must_eq(t, 1, sp_da_size(errs));
  u32 count = 0;
  sp_carr_detect_len(it->expect.installed, count, it->expect.installed[count].version);
  if (!count) {
    return SP_OK;
  }
  si_da(spn_err_msvc_install_t) installed = errs[0].err.msvc.installed;
  sp_must_eq(t, count, (u32)si_da_size(installed));
  sp_for(at, count) {
    sp_expect_str_eq_c(t, installed[at].version, it->expect.installed[at].version);
    sp_expect_str_eq_c(t, installed[at].generation, it->expect.installed[at].generation);
  }
  return SP_OK;
}
