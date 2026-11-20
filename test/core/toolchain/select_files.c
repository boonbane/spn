#include "toolchain.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  test_arg_t sdk;
  fixture_sdks_t sdks;
  const c8* toolset;
  struct {
    spn_sanitizer_t sanitizer;
    const c8* files [FIXTURE_MAX_ARGS];
  } declared;
  fixture_files_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "detected_install_lists_the_redist_of_the_selected_toolset",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .redist = { "/X/A", "/X/B" } }, { { "/Y" }, SPN_ARCH_X64, "14.29", .redist = { "/Y/A" } } } },
    .toolset = "v142",
    .expect.c = { "/Y/A" },
  },
  {
    .name = "msvc_driver_lists_the_asan_runtime_of_the_selected_toolset",
    .driver = SPN_CC_DRIVER_MSVC,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44" }, { { "/Y" }, SPN_ARCH_X64, "14.29" } } },
    .toolset = "v142",
    .expect.sanitizers = { { SPN_SANITIZER_ADDRESS, { "/Y/VC/Tools/MSVC/14.29/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll" } } },
  },
  {
    .name = "another_driver_on_that_install_lists_no_asan_runtime",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44" } } },
  },
  {
    .name = "sanitizer_runtime_brings_the_redist_of_the_install",
    .driver = SPN_CC_DRIVER_MSVC,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .redist = { "/X/A" } } } },
    .expect = {
      .c = { "/X/A" },
      .sanitizers = { { SPN_SANITIZER_ADDRESS, { "/X/VC/Tools/MSVC/14.44/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll", "/X/A" } } },
    },
  },
  {
    .name = "declared_sanitizer_runtime_brings_the_redist_of_a_detected_install",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .redist = { "/X/A" } } } },
    .declared = { SPN_SANITIZER_ADDRESS, { "/B" } },
    .expect = {
      .c = { "/X/A" },
      .sanitizers = { { SPN_SANITIZER_ADDRESS, { "/B", "/X/A" } } },
    },
  },
  {
    .name = "explicit_sdk_lists_no_runtime",
    .driver = SPN_CC_DRIVER_MSVC,
    .sdk = { .path = "/S" },
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .redist = { "/X/A" } } } },
  },
};

sp_test_each(select_files, msvc, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_toolchain_decl_t toolchain = fixture_local_toolchain("A", (fixture_launcher_t) { .name = "cc" });
  toolchain.driver = it->driver;
  toolchain.host_row = false;
  spn_toolchain_target_t target = fixture_target((fixture_target_t) { .triple = TARGET_WIN_MSVC, .sdk = it->sdk });
  sp_for(slot, si_sanitizer_count) {
    if ((u32)it->declared.sanitizer != 1u << slot) {
      continue;
    }
    target.sanitizers.files[slot] = sp_da_new(mem, spn_arg_t);
    sp_carr_for(it->declared.files, at) {
      if (!it->declared.files[at]) {
        break;
      }
      sp_da_push(target.sanitizers.files[slot], spn_arg_path((spn_path_t) { .sub = sp_cstr_as_str(it->declared.files[at]) }));
    }
  }
  toolchain.targets = sp_da_new(mem, spn_toolchain_target_t);
  sp_da_push(toolchain.targets, target);

  spn_toolchain_catalog_t catalog = sp_zero;
  spn_toolchain_catalog_init(&catalog, (spn_triple_t) HOST_X64_LINUX, fixture_sdks(mem, it->sdks), mem);
  spn_toolchain_catalog_add(&catalog, toolchain);

  spn_toolchain_candidate_t candidate = { .triple = TARGET_WIN_MSVC };
  sp_must_eq(t, (u32)SPN_LINKING_REFUSAL_NONE, (u32)si_ld_linkings(candidate.triple, sp_zero_s(spn_linking_t), SPN_LIB_KIND_NONE, &candidate.linkings));
  spn_toolchain_query_t query = {
    .toolchain = spn_toolchain_ref_from_str(sp_str_lit("A")),
    .target = TARGET_WIN_MSVC,
    .candidates = { .items = { candidate }, .count = 1 },
    .msvc.toolset = sp_cstr_as_str(it->toolset),
  };
  spn_toolchain_selection_t selection = sp_zero;
  sp_must_eq(t, (u32)SPN_OK, (u32)si_toolchain_select(&catalog, query, &selection));
  return fixture_check_files(t, &selection.row.files, &it->expect);
}
