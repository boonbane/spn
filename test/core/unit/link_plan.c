#include "unit.h"

#include "manifest.h"
#include "session/session.h"
#include "triple/triple.h"

sp_test_suite(link_plan, .serial = true);

// The examined link unit; it lands on the root package as a target named "app"
typedef struct {
  spn_target_kind_t kind;
  spn_linkage_set_t linkages;
  const c8* source [UNIT_TEST_MAX_STRS];
  const c8* deps [UNIT_TEST_MAX_STRS];
  const c8* system_deps [UNIT_TEST_MAX_STRS];
  const c8* frameworks [UNIT_TEST_MAX_STRS];
  const c8* link_flags [UNIT_TEST_MAX_STRS];
  const c8* linker_script [UNIT_TEST_MAX_STRS];
  spn_os_version_t min_os;
} plan_target_t;

typedef struct {
  const c8* lib;
  spn_link_role_t role;
} plan_dep_t;

typedef struct {
  plan_dep_t deps [UNIT_TEST_MAX_STRS];
  const c8* system_libs [UNIT_TEST_MAX_STRS];
  const c8* frameworks [UNIT_TEST_MAX_STRS];
  const c8* args [UNIT_TEST_MAX_STRS];
  const c8* scripts [UNIT_TEST_MAX_STRS];
  spn_os_version_t min_os;
  spn_lang_t lang;
  spn_err_t err;
  spn_cc_feature_t feature;
} plan_expect_t;

typedef struct {
  const c8* name;
  plan_target_t target;
  unit_graph_test_t graph;
  plan_expect_t expect;
} plan_test_t;

static const plan_test_t tests [] = {
  // An executable takes everything on the link line: static and shared libs
  // both, private or not, while object, source, and no_link libs stay off
  {
    .name = "exe_places_all_deps_on_link_line",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } },
    .graph = {
      .pkgs = {
        {
          .name = "P1",
          .deps = {
            { "D1" }, { "D2" }, { "D3", .private = true }, { "D4" }, { "D5" }, { "D6" }
          },
          .system_deps = { "pthread" }
        },
        { .name = "D1", .libs = { { "D1", STATIC_ONLY, .source = { "a.c" } } }, .system_deps = { "m" } },
        { .name = "D2", .libs = { { "D2", SHARED_ONLY, .source = { "s.c" } } } },
        { .name = "D3", .libs = { { "D3", STATIC_ONLY, .source = { "p.c" } } } },
        { .name = "D4", .libs = { { "D4", OBJECT_ONLY, .source = { "b.c" } } } },
        { .name = "D5", .libs = { { "D5", SOURCE_ONLY, .source = { "r.c" } } } },
        { .name = "D6", .libs = { { "D6", STATIC_ONLY, .no_link = true, .source = { "t.c" } } } },
      },
    },
    .expect = {
      .deps = { { "D3" }, { "D2", SPN_LINK_ROLE_SHARED }, { "D1" } },
      .system_libs = { "pthread", "m" },
    },
  },
  // A shared lib splits its static deps by privacy: public ones are embedded
  // whole so their symbols re-export, private ones link normally and stay hidden
  {
    .name = "shared_lib_splits_static_deps",
    .target = { .kind = SPN_TARGET_KIND_LIB, .linkages = SHARED_ONLY, .source = { "app.c" } },
    .graph = {
      .pkgs = {
        { .name = "P1", .deps = { { "D1" }, { "D2" }, { "D3", .private = true } } },
        { .name = "D1", .libs = { { "D1", STATIC_ONLY, .source = { "a.c" } } } },
        { .name = "D2", .libs = { { "D2", SHARED_ONLY, .source = { "s.c" } } } },
        { .name = "D3", .libs = { { "D3", STATIC_ONLY, .source = { "p.c" } } } },
      },
    },
    .expect = {
      .deps = { { "D3" }, { "D2", SPN_LINK_ROLE_SHARED }, { "D1", SPN_LINK_ROLE_WHOLE_ARCHIVE } },
    },
  },
  {
    .name = "siblings_link_before_packages",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .deps = { "L1" } },
    .graph = {
      .pkgs = {
        { .name = "P1",
          .deps = { { "D1" } },
          .libs = {
            { "L1", STATIC_ONLY, .source = { "u.c" }, .deps = { "L2" } },
            { "L2", STATIC_ONLY, .source = { "b.c" } },
          } },
        { .name = "D1", .libs = { { "D1", STATIC_ONLY, .source = { "s.c" } } } },
      },
    },
    .expect = {
      .deps = { { "L1" }, { "L2" }, { "D1" } },
    },
  },
  {
    .name = "cxx_static_dep_promotes_lang",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } },
    .graph = {
      .pkgs = {
        { .name = "P1", .deps = { { "D1" } } },
        { .name = "D1", .libs = { { "D1", STATIC_ONLY, .source = { "x.cpp" } } } },
      },
    },
    .expect = {
      .deps = { { "D1" } },
      .lang = SPN_LANG_CXX,
    },
  },
  // A shared lib already linked its own C++ runtime; the consumer stays C
  {
    .name = "cxx_shared_dep_keeps_c",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } },
    .graph = {
      .pkgs = {
        { .name = "P1", .deps = { { "D1" } } },
        { .name = "D1", .libs = { { "D1", SHARED_ONLY, .source = { "x.cpp" } } } },
      },
    },
    .expect = {
      .deps = { { "D1", SPN_LINK_ROLE_SHARED } },
    },
  },
  // min_os is the max over the target, every closure package, and every closure
  // lib, including shared boundaries the recursion otherwise stops at
  {
    .name = "min_os_folds_over_closure",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .min_os = { 11, 0 } },
    .graph = {
      .os = SPN_OS_MACOS,
      .sdk = "/sdk",
      .pkgs = {
        { .name = "P1", .deps = { { "D1" }, { "D2" } } },
        { .name = "D1", .min_os = { 12, 5 },
          .libs = { { "D1", STATIC_ONLY, .source = { "a.c" }, .min_os = { 13, 1 } } } },
        { .name = "D2",
          .libs = { { "D2", SHARED_ONLY, .source = { "s.c" }, .min_os = { 14, 0 } } } },
      },
    },
    .expect = {
      .deps = { { "D2", SPN_LINK_ROLE_SHARED }, { "D1" } },
      .min_os = { 14, 0 },
    },
  },
  // Frameworks dedupe, and only packages that link code contribute their
  // package-level frameworks: shared-only and no_link-only packages are skipped
  {
    .name = "frameworks_dedupe_and_respect_links_code",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .frameworks = { "Metal" } },
    .graph = {
      .os = SPN_OS_MACOS,
      .sdk = "/sdk",
      .pkgs = {
        { .name = "P1", .deps = { { "D1" }, { "D2" }, { "D3" } } },
        { .name = "D1", .frameworks = { "Cocoa" },
          .libs = { { "D1", STATIC_ONLY, .source = { "a.c" }, .frameworks = { "Metal", "IOKit" } } } },
        { .name = "D2", .frameworks = { "AppKit" },
          .libs = { { "D2", SHARED_ONLY, .source = { "s.c" }, .frameworks = { "CoreAudio" } } } },
        { .name = "D3", .frameworks = { "Carbon" },
          .libs = { { "L1", STATIC_ONLY, .no_link = true, .source = { "n.c" }, .frameworks = { "Cocoa" } } } },
      },
    },
    .expect = {
      .deps = { { "D2", SPN_LINK_ROLE_SHARED }, { "D1" } },
      .frameworks = { "Metal", "Cocoa", "IOKit" },
    },
  },
  {
    .name = "system_deps_fold_over_targets",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .deps = { "L1" }, .system_deps = { "m" } },
    .graph = {
      .pkgs = {
        { .name = "P1", .deps = { { "D1" }, { "D2" }, { "D3" } },
          .libs = { { "L1", STATIC_ONLY, .source = { "u.c" }, .system_deps = { "pthread" } } } },
        { .name = "D1", .system_deps = { "dl" },
          .libs = { { "D1", STATIC_ONLY, .source = { "a.c" }, .system_deps = { "m", "rt" } } } },
        { .name = "D2", .libs = { { "D2", SHARED_ONLY, .source = { "s.c" }, .system_deps = { "z" } } } },
        { .name = "D3", .libs = { { "L2", STATIC_ONLY, .no_link = true, .source = { "n.c" }, .system_deps = { "png" } } } },
      },
    },
    .expect = {
      .deps = { { "L1" }, { "D2", SPN_LINK_ROLE_SHARED }, { "D1" } },
      .system_libs = { "m", "pthread", "dl", "rt" },
    },
  },
  // Link flags and linker scripts belong to the linked target alone; a dep's
  // own never reach the consumer's link line
  {
    .name = "exe_link_inputs_come_from_target",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .link_flags = { "-A" }, .linker_script = { "A.ld" } },
    .graph = {
      .pkgs = {
        { .name = "P1", .deps = { { "D1" } } },
        { .name = "D1", .libs = { { "D1", SHARED_ONLY, .source = { "s.c" }, .link_flags = { "-B" }, .linker_script = { "B.ld" } } } },
      },
    },
    .expect = {
      .deps = { { "D1", SPN_LINK_ROLE_SHARED } },
      .args = { "-A" },
      .scripts = { "A.ld" },
    },
  },
  // A lib that could be shared but is selected static is archived, not linked,
  // so its link inputs stay off the plan
  {
    .name = "static_selection_drops_link_inputs",
    .target = { .kind = SPN_TARGET_KIND_LIB, .linkages = { .static_lib = true, .shared = true }, .source = { "a.c" }, .link_flags = { "-A" }, .linker_script = { "A.ld" } },
    .graph = {
      .pkgs = {
        { .name = "P1" },
      },
    },
  },
  {
    .name = "test_target_links_test_deps",
    .target = { .kind = SPN_TARGET_KIND_TEST, .source = { "main.c" } },
    .graph = {
      .pkgs = {
        { .name = "P1", .deps = { { "T1", .kind = SPN_DEP_KIND_TEST }, { "D1" } } },
        { .name = "T1", .libs = { { "T1", STATIC_ONLY, .source = { "t.c" } } } },
        { .name = "D1", .libs = { { "D1", STATIC_ONLY, .source = { "a.c" } } } },
      },
    },
    .expect = {
      .deps = { { "D1" }, { "T1" } },
    },
  },
  {
    .name = "frameworks_without_sdk_refused",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .frameworks = { "Metal" } },
    .graph = {
      .os = SPN_OS_MACOS,
      .pkgs = { { .name = "P1" } },
    },
    .expect = {
      .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED,
      .feature = SPN_CC_FEATURE_FRAMEWORKS,
      .frameworks = { "Metal" },
    },
  },
  {
    .name = "linker_script_on_macos_refused",
    .target = { .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .linker_script = { "A.ld" } },
    .graph = {
      .os = SPN_OS_MACOS,
      .sdk = "/sdk",
      .pkgs = { { .name = "P1" } },
    },
    .expect = {
      .err = SPN_ERR_COMPILER_FEATURE_UNSUPPORTED,
      .feature = SPN_CC_FEATURE_LINKER_SCRIPT,
    },
  },
};

static void target_decl(sp_mem_t mem, const plan_target_t* spec, si_target_info_t* decl) {
  decl->linkages = spec->linkages;
  decl->source = test_gated_sources(mem, spec->source, UNIT_TEST_MAX_STRS);
  decl->deps = test_gated_list(mem, spec->deps, UNIT_TEST_MAX_STRS);
  decl->system_deps = test_gated_list(mem, spec->system_deps, UNIT_TEST_MAX_STRS);
  decl->macos.frameworks = test_gated_list(mem, spec->frameworks, UNIT_TEST_MAX_STRS);
  decl->macos.min_os = spec->min_os;
  decl->link_flags = test_gated_list(mem, spec->link_flags, UNIT_TEST_MAX_STRS);
  decl->linker_script = test_gated_paths(mem, spec->linker_script, UNIT_TEST_MAX_STRS);
}

static sp_err_t expect_path_suffixes(sp_test_t* t, sp_da(spn_path_t) actual, const c8* const* expect, u32 count) {
  sp_must_eq(t, count, sp_da_size(actual));
  sp_for(it, count) {
    sp_expect(t, sp_str_ends_with(actual[it].sub, sp_str_view(expect[it])));
  }
  return SP_OK;
}

sp_test_each(link_plan, plan, plan_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_session_t* s = build_session(mem, &it->graph);

  si_pkg_info_t* manifest = test_manifest(s, find_pkg_id(s, &it->graph, it->graph.pkgs[0].name));
  si_target_info_t* decl = SP_NULLPTR;
  decl = test_manifest_target(s->mem, manifest, sp_str_lit("app"), it->target.kind);
  target_decl(mem, &it->target, decl);

  spn_err_t err = test_plan(s, sp_str_lit("app"), it->target.kind);
  sp_must_eq(t, it->expect.err, err);
  if (err) {
    sp_da(spn_event_t) errs = spn_test_drain_errs(mem);
    sp_must_eq(t, 1, sp_da_size(errs));
    sp_expect_eq(t, err, errs[0].err.kind);
    spn_err_compiler_t* refusal = &errs[0].err.compiler;
    sp_expect_eq(t, it->expect.feature, refusal->feature);
    sp_expect_str_eq_c(t, refusal->toolchain, "test");
    sp_expect_str_eq_c(t, refusal->pkg, "P1");
    sp_expect_str_eq_c(t, refusal->name, "app");
    sp_expect(t, spn_triple_equal(refusal->target, spn_profile_triple(&s->units.target->profile)));
    sp_must_strs_eq(t, refusal->frameworks, sp_da_size(refusal->frameworks), it->expect.frameworks);
    return SP_OK;
  }

  sp_must_eq(t, 1, si_da_size(s->plan.closure.roots));
  spn_target_unit_t* app = s->plan.closure.roots[0];

  spn_link_plan_t* plan = &app->link;
  u32 num_deps = 0;
  sp_carr_detect_len(it->expect.deps, num_deps, it->expect.deps[num_deps].lib);
  sp_must_eq(t, num_deps, si_da_size(plan->deps));
  sp_for(dt, num_deps) {
    sp_expect_str_eq_c(t, plan->deps[dt].lib->target->info->name.str, it->expect.deps[dt].lib);
    sp_expect_eq(t, it->expect.deps[dt].role, plan->deps[dt].role);
  }
  sp_must_strs_eq(t, plan->cc.system_libs, sp_da_size(plan->cc.system_libs), it->expect.system_libs);
  sp_must_strs_eq(t, plan->cc.frameworks, sp_da_size(plan->cc.frameworks), it->expect.frameworks);
  sp_must_strs_eq(t, plan->cc.args, sp_da_size(plan->cc.args), it->expect.args);
  u32 num_scripts = 0;
  sp_carr_detect_len(it->expect.scripts, num_scripts, it->expect.scripts[num_scripts]);
  sp_try(expect_path_suffixes(t, plan->cc.scripts, it->expect.scripts, num_scripts));
  sp_expect_eq(t, it->expect.min_os.major, plan->cc.min_os.major);
  sp_expect_eq(t, it->expect.min_os.minor, plan->cc.min_os.minor);
  sp_expect_eq(t, it->expect.lang, plan->cc.lang);

  return SP_OK;
}
