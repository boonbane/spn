#include "unit.h"

#include "triple/triple.h"

typedef struct {
  spn_err_t err;
  spn_linkage_requester_t requester;
  spn_cc_feature_t feature;
  const c8* frameworks [UNIT_TEST_MAX_STRS];
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
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_TARGET },
  },
  {
    .name = "static_libc_blames_libc",
    .graph = {
      .request = { .libc = SPN_RUNTIME_STATIC },
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", shared_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_LIBC },
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
    .expect = { .err = SPN_ERR_TARGET_LINKAGE, .requester = SPN_LINKAGE_REQUESTER_PROFILE },
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
    .name = "unknown_target_dep",
    .graph.pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .deps = { "Z" } } } } },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = { .err = SPN_ERR_TARGET_DEP },
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
    .name = "object_lib_bypasses_profile_linkage",
    .graph = {
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "D" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } },
        { .name = "D", .targets = { { "D", object_only, .source = { "d.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
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
    .name = "target_dep_naming_a_package_is_not_a_sibling",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" }, .deps = { "B" } } } },
      { .name = "B", .targets = { { "B", static_only, .source = { "b.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
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

sp_test_each(unit_errors, plan, test_t, tests, .setup = spn_test_ctx_setup) {
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
      break;
    }
    case SPN_ERR_COMPILER_FEATURE_UNSUPPORTED: {
      spn_err_compiler_t* refusal = &errs[0].err.compiler;
      sp_expect_eq(t, it->expect.feature, refusal->feature);
      sp_expect_str_eq_c(t, refusal->toolchain, "T");
      sp_expect_str_eq_c(t, refusal->pkg, "P");
      sp_expect_str_eq_c(t, refusal->name, "A");
      sp_expect(t, spn_triple_equal(refusal->target, spn_profile_triple(&u->session.builds.program->profile)));
      sp_must_strs_eq(t, refusal->frameworks, sp_da_size(refusal->frameworks), it->expect.frameworks);
      break;
    }
    default: {
      break;
    }
  }
  return SP_OK;
}
