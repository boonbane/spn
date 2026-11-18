#include "unit.h"

#include "triple/triple.h"

typedef struct {
  spn_err_t err;
  spn_linkage_requester_t requester;
  const c8* consumer;
  spn_cc_feature_t feature;
  const c8* frameworks [UNIT_TEST_MAX_STRS];
  const c8* source;
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t root;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  // Nothing in the profile asked for static linkage; the target's row derived it
  {
    .name = "derived_static_blames_the_target",
    .graph = {
      .abi = SPN_ABI_MUSL,
      .linkage = SPN_LIB_KIND_STATIC,
      .libc = SPN_RUNTIME_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_TARGET, .consumer = "A" },
  },
  {
    .name = "static_libc_blames_libc",
    .graph = {
      .abi = SPN_ABI_MUSL,
      .request = { .libc = SPN_RUNTIME_STATIC },
      .linkage = SPN_LIB_KIND_STATIC,
      .libc = SPN_RUNTIME_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_LIBC, .consumer = "A" },
  },
  {
    .name = "profile_linkage_blames_the_profile",
    .graph = {
      .request = { .linkage = SPN_LIB_KIND_STATIC },
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_PROFILE, .consumer = "A" },
  },
  {
    .name = "missing_loader_outranks_the_profile",
    .graph = {
      .os = SPN_OS_WASI,
      .abi = SPN_ABI_MUSL,
      .request = { .linkage = SPN_LIB_KIND_SHARED },
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_NO_LOADER, .consumer = "A" },
  },
  {
    .name = "root_lib_with_no_producible_kind",
    .graph = {
      .abi = SPN_ABI_MUSL,
      .request = { .libc = SPN_RUNTIME_STATIC },
      .linkage = SPN_LIB_KIND_STATIC,
      .libc = SPN_RUNTIME_STATIC,
      .pkgs = { { .name = "P", .targets = { { "A", shared_only, .source = { "a.c" } } } } },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_LIBC },
  },
  {
    .name = "pinned_shared_lib_without_a_loader",
    .graph = {
      .abi = SPN_ABI_MUSL,
      .linkage = SPN_LIB_KIND_STATIC,
      .libc = SPN_RUNTIME_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .config = SPN_LIB_KIND_SHARED, .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_TARGET, .consumer = "A" },
  },
  {
    .name = "pinned_shared_lib_admitted_under_static_profile",
    .graph = {
      .request = { .linkage = SPN_LIB_KIND_STATIC },
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .config = SPN_LIB_KIND_SHARED, .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  {
    .name = "unlinked_shared_lib_is_not_refused",
    .graph = {
      .request = { .linkage = SPN_LIB_KIND_STATIC },
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = { { .name = "P", .targets = { { "A", shared_only, .source = { "a.c" } } } } },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
  },
  {
    .name = "archive_does_not_link_its_shared_dep",
    .graph = {
      .request = { .linkage = SPN_LIB_KIND_STATIC },
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", static_only, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
  },
  {
    .name = "derived_shared_admits_a_shared_lib",
    .graph = {
      .linkage = SPN_LIB_KIND_SHARED,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  {
    .name = "frameworks_without_sdk_refused",
    .graph = {
      .os = SPN_OS_MACOS,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .frameworks = { "Metal" } } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = SPN_CC_FEATURE_FRAMEWORKS, .frameworks = { "Metal" } },
  },
  {
    .name = "linker_script_on_macos_refused",
    .graph = {
      .os = SPN_OS_MACOS,
      .sdk = "/sdk",
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .linker_script = { "A.ld" } } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED, .feature = SPN_CC_FEATURE_LINKER_SCRIPT },
  },
  {
    .name = "cxx_source_without_cxx_compiler",
    .graph = {
      .no_cxx = true,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.cpp" } } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TOOLCHAIN_NO_CXX },
  },
  {
    .name = "config_kind_blames_the_root_manifest",
    .graph.pkgs = {
      { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "D", .config = SPN_LIB_KIND_STATIC, .targets = { { "D", shared_only, .source = { "d.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_ROOT_MANIFEST },
  },
  {
    .name = "malformed_source_glob",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "[" } } } } },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_SOURCE_GLOB },
  },
  {
    .name = "staged_name_collision",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" }, { "C" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "L", shared_only, .source = { "b.c" } } } },
      { .name = "C", .targets = { { "L", shared_only, .source = { "c.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_COLLISION },
  },
  {
    .name = "two_shared_entries_survive",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .shared = { { .stub = "B.tbd" }, { .stub = "C.tbd" } } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_PREBUILTS, .source = "C.tbd" },
  },
  {
    .name = "shared_file_needs_stub_where_dlls_link_by_import_lib",
    .graph = {
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .driver = SPN_CC_DRIVER_ZIG,
      .runtime = SPN_RUNTIME_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "B", .targets = { { "B", shared_only, .shared = { { .path = "B.dll" } } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_STUB_REQUIRED, .source = "B.dll" },
  },
  {
    .name = "configure_source_missing",
    .graph.pkgs = {
      { .name = "P", .targets = {
        { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } },
        { "C", .kind = SPN_TARGET_KIND_CONFIGURE_METAPROGRAM, .source = { "c.c" } },
      } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_CONFIGURE_SOURCE_MISSING },
  },
  {
    .name = "configure_source_glob_matches_nothing",
    .graph.pkgs = {
      { .name = "P", .targets = {
        { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } },
        { "C", .kind = SPN_TARGET_KIND_CONFIGURE_METAPROGRAM, .source = { "tools/*.c" } },
      } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_CONFIGURE_SOURCE_GLOB },
  },
};

sp_test_each(unit_errors, plan, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  spn_err_t err = units_add_roots(u, &it->root, 1, &targets);
  if (!err) {
    err = units_plan(u, targets);
  }
  sp_must_eq(t, it->expect.err, err);

  sp_da(spn_event_t) errs = units_errs(u);
  if (!err) {
    sp_must_eq(t, 0, (u32)sp_da_size(errs));
    return SP_OK;
  }
  sp_must_eq(t, 1, (u32)sp_da_size(errs));
  sp_expect_eq(t, err, errs[0].err.kind);
  switch (err) {
    case SPN_ERR_TARGET_LINKAGE: {
      sp_expect_eq(t, it->expect.requester, errs[0].err.target.requester);
      sp_expect_str_eq_c(t, errs[0].err.target.consumer, it->expect.consumer ? it->expect.consumer : "");
      break;
    }
    case SPN_ERR_COMPILER_FEATURE_UNSUPPORTED: {
      spn_err_compiler_t* refusal = &errs[0].err.compiler;
      sp_expect_eq(t, it->expect.feature, refusal->feature);
      sp_expect_str_eq_c(t, refusal->toolchain, "T");
      sp_expect_str_eq_c(t, refusal->pkg, "P");
      sp_expect_str_eq_c(t, refusal->name, "A");
      sp_expect(t, spn_triple_equal(refusal->target, u->session.builds.program->profile.target));
      sp_must_strs_eq(t, refusal->frameworks, sp_da_size(refusal->frameworks), it->expect.frameworks);
      break;
    }
    case SPN_ERR_TARGET_STUB_REQUIRED:
    case SPN_ERR_TARGET_PREBUILTS: {
      spn_err_target_source_t* prebuilts = &errs[0].err.target_source;
      sp_expect_str_eq_c(t, prebuilts->pkg, "B");
      sp_expect_str_eq_c(t, prebuilts->name, "B");
      sp_expect(t, sp_str_ends_with(prebuilts->source, sp_cstr_as_str(it->expect.source)));
      break;
    }
    default: {
      break;
    }
  }
  return SP_OK;
}
