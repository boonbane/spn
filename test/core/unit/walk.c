#include "unit.h"

#include "manifest.h"
#include "pkg/id.h"

#define WALK_TEST_MAX_ROOTS 2
#define WALK_TEST_MAX_UNITS 16

typedef struct {
  const c8* name;
  spn_target_kind_t kind;
  bool metaprogram;
} walk_root_t;

typedef struct {
  const c8* pkg;
  bool metaprogram;
} walk_unit_t;

typedef struct {
  const c8* name;
  unit_graph_test_t graph;
  walk_root_t roots [WALK_TEST_MAX_ROOTS];
  walk_unit_t expect [WALK_TEST_MAX_UNITS];
} walk_test_t;

static const walk_test_t tests [] = {
  {
    .name = "root_package_without_targets_is_in_the_closure",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "A" } } },
        { .name = "A" },
      },
    },
    .expect = {
      { "R" },
    },
  },
  {
    .name = "package_chain_stays_in_target_build",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "A" } } },
        { .name = "A", .deps = { { "B" } } },
        { .name = "B" },
      },
    },
    .roots = { { "app", SPN_TARGET_KIND_EXE } },
    .expect = {
      { "R" },
      { "A" },
      { "B" },
    },
  },
  {
    .name = "test_dep_of_test_target_included",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "T", SPN_DEP_KIND_TEST } } },
        { .name = "T" },
      },
    },
    .roots = { { "app", SPN_TARGET_KIND_TEST } },
    .expect = {
      { "R" },
      { "T" },
    },
  },
  {
    .name = "test_dep_of_exe_target_excluded",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "T", SPN_DEP_KIND_TEST } } },
        { .name = "T" },
      },
    },
    .roots = { { "app", SPN_TARGET_KIND_EXE } },
    .expect = {
      { "R" },
    },
  },
  {
    .name = "test_dep_of_dep_excluded",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "A" } } },
        { .name = "A", .deps = { { "T", SPN_DEP_KIND_TEST } } },
        { .name = "T" },
      },
    },
    .roots = { { "app", SPN_TARGET_KIND_TEST } },
    .expect = {
      { "R" },
      { "A" },
    },
  },
  {
    .name = "build_dep_of_build_script_lands_in_metaprogram_build",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "T", SPN_DEP_KIND_BUILD } } },
        { .name = "T" },
      },
    },
    .roots = { { "build", SPN_TARGET_KIND_BUILD_METAPROGRAM, .metaprogram = true } },
    .expect = {
      { "R" },
      { "R", .metaprogram = true },
      { "T", .metaprogram = true },
    },
  },
  {
    .name = "build_dep_of_exe_target_excluded",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "T", SPN_DEP_KIND_BUILD } } },
        { .name = "T" },
      },
    },
    .roots = { { "app", SPN_TARGET_KIND_EXE } },
    .expect = {
      { "R" },
    },
  },
  {
    .name = "configure_script_follows_no_edges",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "A" }, { "T", SPN_DEP_KIND_BUILD } } },
        { .name = "A" },
        { .name = "T" },
      },
    },
    .roots = { { "configure", SPN_TARGET_KIND_CONFIGURE_METAPROGRAM, .metaprogram = true } },
    .expect = {
      { "R" },
      { "R", .metaprogram = true },
    },
  },
  // A build dep is consumed as a library, so only its package deps follow;
  // its own build deps belong to its build script, which nothing here roots
  {
    .name = "build_dep_follows_package_deps_only",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "T", SPN_DEP_KIND_BUILD } } },
        { .name = "T", .deps = { { "U" }, { "G", SPN_DEP_KIND_BUILD } } },
        { .name = "U" },
        { .name = "G" },
      },
    },
    .roots = { { "build", SPN_TARGET_KIND_BUILD_METAPROGRAM, .metaprogram = true } },
    .expect = {
      { "R" },
      { "R", .metaprogram = true },
      { "T", .metaprogram = true },
      { "U", .metaprogram = true },
    },
  },
  {
    .name = "dual_membership_gets_two_units",
    .graph = {
      .pkgs = {
        { .name = "R", .deps = { { "A" }, { "A", SPN_DEP_KIND_BUILD } } },
        { .name = "A" },
      },
    },
    .roots = {
      { "app", SPN_TARGET_KIND_EXE },
      { "build", SPN_TARGET_KIND_BUILD_METAPROGRAM, .metaprogram = true },
    },
    .expect = {
      { "R" },
      { "A" },
      { "R", .metaprogram = true },
      { "A", .metaprogram = true },
    },
  },
};

sp_test_each(unit, walk, walk_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_session_t* s = build_session(mem, &it->graph);
  spn_pkg_id_t id = find_pkg_id(s, &it->graph, it->graph.pkgs[0].name);
  si_pkg_info_t* manifest = test_manifest(s, id);

  u32 count = 0;
  sp_carr_detect_len(it->roots, count, it->roots[count].name);
  sp_for(rt, count) {
    test_manifest_target(s->mem, manifest, sp_cstr_as_str(it->roots[rt].name), it->roots[rt].kind);
  }

  si_da(spn_target_unit_t*) roots = sp_zero;
  sp_for(rt, count) {
    const walk_root_t* root = &it->roots[rt];
    spn_build_unit_t* build = root->metaprogram ? s->builds.metaprogram : s->builds.program;
    spn_pkg_unit_t* pkg = spn_pkg_unit_ensure(&s->units, build, sp_ht_getp(s->resolve, id));
    si_target_t* target = spn_pkg_get_target(&pkg->pkg, sp_cstr_as_str(root->name), root->kind);
    sp_must(t, target);
    spn_target_unit_t* unit = SP_NULLPTR;
    sp_must_eq(t, SPN_OK, spn_target_unit_ensure(&s->units, pkg, target, &unit));
    si_da_push(mem, roots, unit);
  }

  spn_closure_t closure = sp_zero;
  sp_must_eq(t, SPN_OK, spn_closure_new(&s->units, spn_pkg_unit_ensure(&s->units, s->builds.program, sp_ht_getp(s->resolve, id)), roots, &closure));

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].pkg);
  sp_must_eq(t, expected, si_da_size(closure.pkgs));

  sp_for(et, expected) {
    const walk_unit_t* expect = &it->expect[et];
    sp_test_kv_c(t, "pkg", expect->pkg);
    spn_pkg_unit_t* unit = closure.pkgs[et];
    sp_expect(t, spn_pkg_id_eq(unit->id.pkg, find_pkg_id(s, &it->graph, expect->pkg)));
    sp_expect_eq(t, expect->metaprogram, unit->build == s->builds.metaprogram);
  }

  return SP_OK;
}
