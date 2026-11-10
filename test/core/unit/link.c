#include "unit.h"

typedef struct {
  const c8* lib;
  spn_link_role_t role;
} dep_t;

typedef struct {
  dep_t deps [UNIT_TEST_MAX_STRS];
  const c8* system_libs [UNIT_TEST_MAX_STRS];
  const c8* frameworks [UNIT_TEST_MAX_STRS];
  const c8* args [UNIT_TEST_MAX_STRS];
  const c8* scripts [UNIT_TEST_MAX_STRS];
  spn_os_version_t min_os;
  spn_lang_t lang;
} expect_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t root;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  // An executable takes everything on the link line: static and shared libs
  // both, private or not, while object, source, and no_link libs stay off
  {
    .name = "exe_places_all_deps_on_link_line",
    .graph.pkgs = {
      {
        .name = "P",
        .deps = { { "B" }, { "C" }, { "D", .private = true }, { "E" }, { "F" }, { "G" } },
        .system_deps = { "pthread" },
        .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } },
      },
      { .name = "B", .targets = { { "B", static_only, .source = { "a.c" } } }, .system_deps = { "m" } },
      { .name = "C", .targets = { { "C", shared_only, .source = { "s.c" } } } },
      { .name = "D", .targets = { { "D", static_only, .source = { "p.c" } } } },
      { .name = "E", .targets = { { "E", object_only, .source = { "b.c" } } } },
      { .name = "F", .targets = { { "F", source_only, .source = { "r.c" } } } },
      { .name = "G", .targets = { { "G", static_only, .no_link = true, .source = { "t.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "D" }, { "C", SPN_LINK_ROLE_SHARED }, { "B" } },
      .system_libs = { "pthread", "m" },
    },
  },
  // A shared lib splits its static deps by privacy: public ones are embedded
  // whole so their symbols re-export, private ones link normally and stay hidden
  {
    .name = "shared_lib_splits_static_deps",
    .graph.pkgs = {
      {
        .name = "P",
        .deps = { { "B" }, { "C" }, { "D", .private = true } },
        .targets = { { "A", shared_only, .source = { "app.c" } } },
      },
      { .name = "B", .targets = { { "B", static_only, .source = { "a.c" } } } },
      { .name = "C", .targets = { { "C", shared_only, .source = { "s.c" } } } },
      { .name = "D", .targets = { { "D", static_only, .source = { "p.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
    .expect = {
      .deps = { { "D" }, { "C", SPN_LINK_ROLE_SHARED }, { "B", SPN_LINK_ROLE_WHOLE_ARCHIVE } },
    },
  },
  {
    .name = "siblings_link_before_packages",
    .graph.pkgs = {
      {
        .name = "P",
        .deps = { { "B" } },
        .targets = {
          { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .deps = { "L" } },
          { "L", static_only, .source = { "u.c" }, .deps = { "M" } },
          { "M", static_only, .source = { "b.c" } },
        },
      },
      { .name = "B", .targets = { { "B", static_only, .source = { "s.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "L" }, { "M" }, { "B" } },
    },
  },
  {
    .name = "cxx_static_dep_promotes_lang",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .targets = { { "B", static_only, .source = { "x.cpp" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B" } },
      .lang = SPN_LANG_CXX,
    },
  },
  // A shared lib already linked its own C++ runtime; the consumer stays C
  {
    .name = "cxx_shared_dep_keeps_c",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "x.cpp" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B", SPN_LINK_ROLE_SHARED } },
    },
  },
  // min_os is the max over the target, every closure package, and every closure
  // lib, including shared boundaries the recursion otherwise stops at
  {
    .name = "min_os_folds_over_closure",
    .graph = {
      .os = SPN_OS_MACOS,
      .sdk = "/sdk",
      .pkgs = {
        { .name = "P", .deps = { { "B" }, { "C" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .min_os = { 11, 0 } } } },
        { .name = "B", .min_os = { 12, 5 }, .targets = { { "B", static_only, .source = { "a.c" }, .min_os = { 13, 1 } } } },
        { .name = "C", .targets = { { "C", shared_only, .source = { "s.c" }, .min_os = { 14, 0 } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "C", SPN_LINK_ROLE_SHARED }, { "B" } },
      .min_os = { 14, 0 },
    },
  },
  // Frameworks dedupe, and only packages that link code contribute their
  // package-level frameworks: shared-only and no_link-only packages are skipped
  {
    .name = "frameworks_dedupe_and_respect_links_code",
    .graph = {
      .os = SPN_OS_MACOS,
      .sdk = "/sdk",
      .pkgs = {
        { .name = "P", .deps = { { "B" }, { "C" }, { "D" }, { "E" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .frameworks = { "Metal" } } } },
        { .name = "B", .frameworks = { "Cocoa" }, .targets = { { "B", static_only, .source = { "a.c" }, .frameworks = { "Metal", "IOKit" } } } },
        { .name = "C", .frameworks = { "AppKit" }, .targets = { { "C", shared_only, .source = { "s.c" }, .frameworks = { "CoreAudio" } } } },
        { .name = "D", .frameworks = { "Carbon" }, .targets = { { "L", static_only, .no_link = true, .source = { "n.c" }, .frameworks = { "Cocoa" } } } },
        { .name = "E", .frameworks = { "OpenGL" } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "C", SPN_LINK_ROLE_SHARED }, { "B" } },
      .frameworks = { "Metal", "OpenGL", "Cocoa", "IOKit" },
    },
  },
  {
    .name = "system_deps_fold_over_targets",
    .graph.pkgs = {
      {
        .name = "P",
        .deps = { { "B" }, { "C" }, { "D" } },
        .targets = {
          { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .deps = { "L" }, .system_deps = { "m" } },
          { "L", static_only, .source = { "u.c" }, .system_deps = { "pthread" } },
        },
      },
      { .name = "B", .system_deps = { "dl" }, .targets = { { "B", static_only, .source = { "a.c" }, .system_deps = { "m", "rt" } } } },
      { .name = "C", .targets = { { "C", shared_only, .source = { "s.c" }, .system_deps = { "z" } } } },
      { .name = "D", .targets = { { "M", static_only, .no_link = true, .source = { "n.c" }, .system_deps = { "png" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "L" }, { "C", SPN_LINK_ROLE_SHARED }, { "B" } },
      .system_libs = { "m", "pthread", "dl", "rt" },
    },
  },
  // Link flags and linker scripts belong to the linked target alone; a dep's
  // own never reach the consumer's link line
  {
    .name = "exe_link_inputs_come_from_target",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .link_flags = { "-A" }, .linker_script = { "A.ld" } } } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "s.c" }, .link_flags = { "-B" }, .linker_script = { "B.ld" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B", SPN_LINK_ROLE_SHARED } },
      .args = { "-A" },
      .scripts = { "A.ld" },
    },
  },
  // A lib that could be shared but is selected static is archived, not linked,
  // so its link inputs stay off the plan
  {
    .name = "static_selection_drops_link_inputs",
    .graph.pkgs = {
      { .name = "P", .targets = { { "A", { .static_lib = true, .shared = true }, .source = { "a.c" }, .link_flags = { "-A" }, .linker_script = { "A.ld" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
  },
  {
    .name = "test_target_links_test_deps",
    .graph.pkgs = {
      { .name = "P", .deps = { { "T", SPN_DEP_KIND_TEST }, { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_TEST, .source = { "main.c" } } } },
      { .name = "T", .targets = { { "T", static_only, .source = { "t.c" } } } },
      { .name = "B", .targets = { { "B", static_only, .source = { "a.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_TEST },
    .expect = {
      .deps = { { "B" }, { "T" } },
    },
  },
};

static sp_err_t expect_path_suffixes(sp_test_t* t, sp_da(spn_path_t) actual, const c8* const* expect, u32 count) {
  sp_must_eq(t, count, (u32)sp_da_size(actual));
  sp_for(it, count) {
    sp_expect(t, sp_str_ends_with(actual[it].sub, sp_cstr_as_str(expect[it])));
  }
  return SP_OK;
}

sp_test_each(unit_link, plan, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &it->graph);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, &it->root, 1, &targets));
  sp_must_eq(t, SPN_OK, units_plan(u, targets));

  spn_link_plan_t* plan = &targets[0]->link;
  u32 num_deps = 0;
  sp_carr_detect_len(it->expect.deps, num_deps, it->expect.deps[num_deps].lib);
  sp_must_eq(t, num_deps, (u32)si_da_size(plan->deps));
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
