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
  // both, private or not, while no_link libs stay off
  {
    .name = "exe_places_all_deps_on_link_line",
    .graph.pkgs = {
      {
        .name = "P",
        .deps = { { "B", .public = true }, { "C" }, { "D" }, { "E" } },
        .system_deps = { "pthread" },
        .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } },
      },
      { .name = "B", .targets = { { "B", static_only, .source = { "a.c" } } }, .system_deps = { "m" } },
      { .name = "C", .targets = { { "C", shared_only, .source = { "s.c" } } } },
      { .name = "D", .targets = { { "D", static_only, .source = { "p.c" } } } },
      { .name = "E", .targets = { { "E", static_only, .no_link = true, .source = { "t.c" } } } },
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
        .deps = { { "B", .public = true }, { "C" }, { "D" } },
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
  // A stub has no objects, but it is still a shared lib to link against
  {
    .name = "stub_lib_links_shared",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .shared = { { .stub = "B.tbd" } } } } },
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
  // A target's own link takes what it names and what its package names
  {
    .name = "own_requirements_reach_own_link",
    .graph.pkgs = {
      {
        .name = "P",
        .system_deps = { "X" },
        .frameworks = { "F" },
        .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .system_deps = { "Y" }, .frameworks = { "G" } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .system_libs = { "Y", "X" },
      .frameworks = { "G", "F" },
    },
  },
  // A statically linked lib brings its own requirements and its package's
  {
    .name = "static_lib_forwards_requirements",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .system_deps = { "X" }, .frameworks = { "F" }, .targets = { { "B", static_only, .source = { "b.c" }, .system_deps = { "Y" }, .frameworks = { "G" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B" } },
      .system_libs = { "Y", "X" },
      .frameworks = { "G", "F" },
    },
  },
  // A shared lib consumed its requirements at its own link; nothing passes through
  {
    .name = "shared_lib_forwards_no_requirements",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .system_deps = { "X" }, .frameworks = { "F" }, .targets = { { "B", shared_only, .source = { "b.c" }, .system_deps = { "Y" }, .frameworks = { "G" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B", SPN_LINK_ROLE_SHARED } },
    },
  },
  // A package with no lib has no code to carry its requirements to a consumer
  {
    .name = "libless_package_forwards_no_requirements",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .system_deps = { "X" }, .frameworks = { "F" } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  // A lib whose sources all gate off is absent: it brings nothing, and does
  // not count as a linked lib for its package's requirements
  {
    .name = "objectless_lib_forwards_no_requirements",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .system_deps = { "X" }, .frameworks = { "F" }, .targets = { { "B", static_only, .system_deps = { "Y" }, .frameworks = { "G" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  // A lib whose stubs all gate off is absent: it takes no link slot, and no
  // linkage is selected for it, so a static profile does not refuse it
  {
    .name = "gated_off_stub_lib_is_absent",
    .graph = {
      .linkage = SPN_LIB_KIND_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
        { .name = "B", .targets = { { "B", shared_only, .shared = { { .stub = "B.tbd", .os = SPN_OS_MACOS } } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  {
    .name = "no_link_lib_forwards_no_requirements",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .system_deps = { "X" }, .frameworks = { "F" }, .targets = { { "B", static_only, .no_link = true, .source = { "b.c" }, .system_deps = { "Y" }, .frameworks = { "G" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  {
    .name = "requirements_dedupe",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .system_deps = { "X" }, .frameworks = { "F" } } } },
      { .name = "B", .targets = { { "B", static_only, .source = { "b.c" }, .system_deps = { "X" }, .frameworks = { "F" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B" } },
      .system_libs = { "X" },
      .frameworks = { "F" },
    },
  },
  // Published requirements belong to the headers, so a package with no lib
  // still hands them to an image that compiles against it
  {
    .name = "publish_requirements_reach_consumer_of_libless_package",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .publish = { .system_deps = { "X" }, .frameworks = { "F" } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .system_libs = { "X" },
      .frameworks = { "F" },
    },
  },
  {
    .name = "publish_requirements_reach_consumer_of_shared_lib",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .publish = { .system_deps = { "X" }, .frameworks = { "F" } }, .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B", SPN_LINK_ROLE_SHARED } },
      .system_libs = { "X" },
      .frameworks = { "F" },
    },
  },
  // A package's own targets compile against its headers too
  {
    .name = "publish_requirements_reach_own_targets",
    .graph.pkgs = {
      { .name = "P", .publish = { .system_deps = { "X" }, .frameworks = { "F" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .system_libs = { "X" },
      .frameworks = { "F" },
    },
  },
  // The walk stops at shared B, but A compiles against the headers B forwards
  {
    .name = "publish_requirements_follow_forwarded_headers",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .deps = { { "C", .public = true } }, .targets = { { "B", shared_only, .source = { "b.c" } } } },
      { .name = "C", .publish = { .system_deps = { "X" }, .frameworks = { "F" } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B", SPN_LINK_ROLE_SHARED } },
      .system_libs = { "X" },
      .frameworks = { "F" },
    },
  },
  // A never sees D, but static B compiled against it and B's objects land in A
  {
    .name = "publish_requirements_follow_static_lib_objects",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .deps = { { "C" } }, .targets = { { "B", static_only, .source = { "b.c" } } } },
      { .name = "C", .deps = { { "D", .public = true } }, .targets = { { "C", shared_only, .source = { "c.c" } } } },
      { .name = "D", .publish = { .system_deps = { "X" }, .frameworks = { "F" } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B" }, { "C", SPN_LINK_ROLE_SHARED } },
      .system_libs = { "X" },
      .frameworks = { "F" },
    },
  },
  // A never sees C either, but C's own lib compiled against C's headers
  {
    .name = "publish_requirements_follow_own_lib_objects",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .deps = { { "C" } } },
      { .name = "C", .publish = { .system_deps = { "X" }, .frameworks = { "F" } }, .targets = { { "C", static_only, .source = { "c.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "C" } },
      .system_libs = { "X" },
      .frameworks = { "F" },
    },
  },
  // Nothing in A compiled against C: B has no objects, and its private edge
  // keeps C's headers from A
  {
    .name = "publish_requirements_need_a_compile",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .deps = { { "C" } } },
      { .name = "C", .publish = { .system_deps = { "X" }, .frameworks = { "F" } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
  },
  // Seeing a package's headers through a dep does not put it on the link line:
  // shared B already carries C, and A names no edge to C to re-export it by
  {
    .name = "forwarded_package_is_not_reexported",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", shared_only, .source = { "a.c" } } } },
      { .name = "B", .deps = { { "C", .public = true } }, .targets = { { "B", shared_only, .source = { "b.c" } } } },
      { .name = "C", .targets = { { "C", static_only, .source = { "c.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
    .expect = {
      .deps = { { "B", SPN_LINK_ROLE_SHARED } },
    },
  },
  // Host libs link like system libs, ahead of them
  {
    .name = "own_host_deps_reach_own_link",
    .graph = {
      .sdk_host = true,
      .pkgs = {
        {
          .name = "P",
          .host_deps = { "H" },
          .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .system_deps = { "X" }, .host_deps = { "I" } } },
        },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .system_libs = { "I", "H", "X" },
    },
  },
  {
    .name = "static_lib_forwards_host_deps",
    .graph = {
      .sdk_host = true,
      .pkgs = {
        { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
        { .name = "B", .host_deps = { "H" }, .targets = { { "B", static_only, .source = { "b.c" }, .host_deps = { "I" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B" } },
      .system_libs = { "I", "H" },
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
    .name = "archive_only_lib_drops_link_inputs",
    .graph = {
      .abi = SPN_ABI_MUSL,
      .linkage = SPN_LIB_KIND_STATIC,
      .libc = SPN_RUNTIME_STATIC,
      .pkgs = {
        { .name = "P", .targets = { { "A", both_kinds, .source = { "a.c" }, .link_flags = { "-A" }, .linker_script = { "A.ld" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
  },
  {
    .name = "both_kinds_dep_links_static_under_shared_profile",
    .graph = {
      .linkage = SPN_LIB_KIND_SHARED,
      .pkgs = {
        { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
        { .name = "B", .targets = { { "B", both_kinds, .source = { "a.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B" } },
    },
  },
  {
    .name = "root_lib_image_embeds_public_static_deps_whole",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B", .public = true } }, .targets = { { "A", both_kinds, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", static_only, .source = { "b.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_LIB },
    .expect = {
      .deps = { { "B", SPN_LINK_ROLE_WHOLE_ARCHIVE } },
    },
  },
  {
    .name = "config_pin_links_shared",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .config = SPN_LIB_KIND_SHARED, .targets = { { "B", both_kinds, .source = { "a.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B", SPN_LINK_ROLE_SHARED } },
    },
  },
  {
    .name = "config_pin_without_a_loader_links_static",
    .graph = {
      .abi = SPN_ABI_MUSL,
      .linkage = SPN_LIB_KIND_STATIC,
      .libc = SPN_RUNTIME_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
        { .name = "B", .config = SPN_LIB_KIND_SHARED, .targets = { { "B", both_kinds, .source = { "a.c" } } } },
      },
    },
    .root = { "A", SPN_TARGET_KIND_EXE },
    .expect = {
      .deps = { { "B" } },
    },
  },
  {
    .name = "tool_links_sibling_package_deps",
    .graph.pkgs = {
      {
        .name = "P",
        .deps = { { "B" } },
        .targets = {
          { "A", .kind = SPN_TARGET_KIND_TOOL, .source = { "main.c" }, .deps = { "L" } },
          { "L", static_only, .source = { "u.c" } },
        },
      },
      { .name = "B", .targets = { { "B", static_only, .source = { "a.c" } } } },
    },
    .root = { "A", SPN_TARGET_KIND_TOOL },
    .expect = {
      .deps = { { "L" }, { "B" } },
    },
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
