#include "toolchain.h"

typedef struct {
  const c8* name;
  spn_cc_driver_t driver;
  test_arg_t sdk;
  fixture_sdks_t sdks;
  const c8* toolset;
  fixture_files_t declared;
  spn_runtime_t runtime;
  spn_sanitizer_set_t sanitize;
  struct {
    const c8* c [FIXTURE_MAX_ARGS];
    const c8* cxx [FIXTURE_MAX_ARGS];
  } expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "static_runtime_stages_no_runtime",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdk = { .path = "/S" },
    .declared = { .c = { "/C" }, .cxx = { "/X" } },
    .runtime = SPN_RUNTIME_STATIC,
  },
  {
    .name = "shared_runtime_stages_the_c_runtime_and_the_cxx_runtime_beside_cxx",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdk = { .path = "/S" },
    .declared = { .c = { "/C" }, .cxx = { "/X" } },
    .runtime = SPN_RUNTIME_SHARED,
    .expect = { .c = { "/C" }, .cxx = { "/X" } },
  },
  {
    .name = "sanitizer_stages_its_runtime",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdk = { .path = "/S" },
    .declared.sanitizers = { { SPN_SANITIZER_ADDRESS, { "/D" } }, { SPN_SANITIZER_THREAD, { "/H" } } },
    .sanitize = SPN_SANITIZER_THREAD,
    .expect.c = { "/H" },
  },
  {
    .name = "two_sanitizers_stage_both_runtimes",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdk = { .path = "/S" },
    .declared.sanitizers = { { SPN_SANITIZER_ADDRESS, { "/D" } }, { SPN_SANITIZER_UNDEFINED, { "/U" } } },
    .sanitize = SPN_SANITIZER_ADDRESS | SPN_SANITIZER_UNDEFINED,
    .expect.c = { "/D", "/U" },
  },
  {
    .name = "file_listed_for_two_purposes_is_staged_once",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdk = { .path = "/S" },
    .declared = { .c = { "/C" }, .sanitizers = { { SPN_SANITIZER_ADDRESS, { "/D", "/C" } } } },
    .runtime = SPN_RUNTIME_SHARED,
    .sanitize = SPN_SANITIZER_ADDRESS,
    .expect.c = { "/C", "/D" },
  },
  {
    .name = "detected_install_stages_the_redist_of_the_selected_toolset",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .redist = { "/X/A", "/X/B" } }, { { "/Y" }, SPN_ARCH_X64, "14.29", .redist = { "/Y/A" } } } },
    .toolset = "v142",
    .runtime = SPN_RUNTIME_SHARED,
    .expect.c = { "/Y/A" },
  },
  {
    .name = "msvc_driver_stages_the_asan_runtime_of_the_selected_toolset",
    .driver = SPN_CC_DRIVER_MSVC,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44" }, { { "/Y" }, SPN_ARCH_X64, "14.29" } } },
    .toolset = "v142",
    .sanitize = SPN_SANITIZER_ADDRESS,
    .expect.c = { "/Y/VC/Tools/MSVC/14.29/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll" },
  },
  {
    .name = "another_driver_on_that_install_stages_no_asan_runtime",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44" } } },
    .sanitize = SPN_SANITIZER_ADDRESS,
  },
  {
    .name = "sanitizer_runtime_brings_the_redist_of_the_install",
    .driver = SPN_CC_DRIVER_MSVC,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .redist = { "/X/A" } } } },
    .sanitize = SPN_SANITIZER_ADDRESS,
    .expect.c = { "/X/VC/Tools/MSVC/14.44/bin/Hostx64/x64/clang_rt.asan_dynamic-x86_64.dll", "/X/A" },
  },
  {
    .name = "declared_sanitizer_runtime_brings_the_redist_of_a_detected_install",
    .driver = SPN_CC_DRIVER_CLANG,
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .redist = { "/X/A" } } } },
    .declared.sanitizers = { { SPN_SANITIZER_ADDRESS, { "/B" } } },
    .sanitize = SPN_SANITIZER_ADDRESS,
    .expect.c = { "/B", "/X/A" },
  },
  {
    .name = "explicit_sdk_stages_no_runtime_of_an_install",
    .driver = SPN_CC_DRIVER_MSVC,
    .sdk = { .path = "/S" },
    .sdks = { .msvc = { { { "/X" }, SPN_ARCH_X64, "14.44", .redist = { "/X/A" } } } },
    .runtime = SPN_RUNTIME_SHARED,
    .sanitize = SPN_SANITIZER_ADDRESS,
  },
};

static sp_da(spn_arg_t) declare(sp_mem_t mem, const c8* const* files) {
  sp_da(spn_arg_t) declared = sp_da_new(mem, spn_arg_t);
  sp_for(it, FIXTURE_MAX_ARGS) {
    if (!files[it]) {
      break;
    }
    sp_da_push(declared, spn_arg_path((spn_path_t) { .sub = sp_cstr_as_str(files[it]) }));
  }
  return declared;
}

sp_test_each(select_files, staged, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_toolchain_decl_t toolchain = fixture_local_toolchain("A", (fixture_launcher_t) { .name = "cc" });
  toolchain.driver = it->driver;
  toolchain.host_row = false;
  spn_toolchain_target_t target = fixture_target((fixture_target_t) { .triple = TARGET_WIN_MSVC, .sdk = it->sdk, .sanitizers = it->sanitize, .runtimes = rt_both });
  target.runtime.c = declare(mem, it->declared.c);
  target.runtime.cxx = declare(mem, it->declared.cxx);
  sp_for(slot, si_sanitizer_count) {
    sp_carr_for(it->declared.sanitizers, at) {
      if ((u32)it->declared.sanitizers[at].sanitizer == 1u << slot) {
        target.sanitizers.files[slot] = declare(mem, it->declared.sanitizers[at].files);
      }
    }
  }
  toolchain.targets = sp_da_new(mem, spn_toolchain_target_t);
  sp_da_push(toolchain.targets, target);

  spn_toolchain_catalog_t catalog = sp_zero;
  spn_toolchain_catalog_init(&catalog, (spn_triple_t) HOST_X64_LINUX, fixture_sdks(mem, it->sdks), mem);
  spn_toolchain_catalog_add(&catalog, toolchain);

  spn_toolchain_candidate_t candidate = { .triple = TARGET_WIN_MSVC };
  sp_must_eq(t, (u32)SPN_LINKING_REFUSAL_NONE, (u32)si_ld_linkings(candidate.triple, (spn_linking_t) { .runtime = it->runtime }, SPN_LIB_KIND_NONE, &candidate.linkings));
  spn_toolchain_query_t query = {
    .toolchain = spn_toolchain_ref_from_str(sp_str_lit("A")),
    .target = TARGET_WIN_MSVC,
    .candidates = { .items = { candidate }, .count = 1 },
    .sanitizers = it->sanitize,
    .msvc.toolset = sp_cstr_as_str(it->toolset),
  };
  spn_toolchain_selection_t selection = sp_zero;
  sp_must_eq(t, (u32)SPN_OK, (u32)si_toolchain_select(&catalog, query, &selection));

  u32 c = 0;
  sp_carr_detect_len(it->expect.c, c, it->expect.c[c]);
  u32 cxx = 0;
  sp_carr_detect_len(it->expect.cxx, cxx, it->expect.cxx[cxx]);
  sp_try(fixture_check_paths(t, selection.staged.c, it->expect.c));
  sp_must_eq(t, c + cxx, (u32)si_da_size(selection.staged.cxx));
  sp_for(at, c) {
    sp_expect_str_eq_c(t, selection.staged.cxx[at].sub, it->expect.c[at]);
  }
  sp_for(at, cxx) {
    sp_expect_str_eq_c(t, selection.staged.cxx[c + at].sub, it->expect.cxx[at]);
  }
  return SP_OK;
}
