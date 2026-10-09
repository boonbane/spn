#include "unit.h"

#include "pkg/pkg.h"

typedef struct {
  const c8* name;
  unit_graph_test_t graph;
} clone_test_t;

static clone_test_t tests [] = {
  {
    .name = "apply_isolates_units",
    .graph = {
      .pkgs = {
        { .name = "R", .scripts = true, .libs = { { .name = "L", STATIC_ONLY, .source = { "a.c" } } } },
      },
    },
  },
};

sp_test_each(unit, clone, clone_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_session_t* s = build_session(mem, &it->graph);
  spn_pkg_id_t id = find_pkg_id(s, &it->graph, "R");
  spn_pkg_info_t* loaded = sp_ht_getp(s->packages, id)->info;
  spn_target_info_t* loaded_lib = spn_pkg_get_target(loaded, sp_str_lit("L"), SPN_TARGET_KIND_LIB);
  sp_must(t, loaded_lib);

  si_da_push(mem, loaded->gated.define, ((spn_gated_str_t) { .value = sp_str_lit("A") }));
  si_da_push(mem, loaded->gated.system_deps, ((spn_gated_str_t) { .value = sp_str_lit("A") }));
  si_da_push(mem, loaded->gated.frameworks, ((spn_gated_str_t) { .value = sp_str_lit("A") }));
  si_da_push(mem, loaded->gated.include, ((spn_gated_path_t) { .path = sp_str_lit("A"), .tree = SPN_TREE_SOURCE }));
  si_da_push(mem, loaded->gated.publish, ((spn_gated_publish_t) { .source = { .path = sp_str_lit("A"), .tree = SPN_TREE_SOURCE } }));
  si_da_push(mem, loaded_lib->gated.define, ((spn_gated_str_t) { .value = sp_str_lit("A") }));
  si_da_push(mem, loaded_lib->gated.frameworks, ((spn_gated_str_t) { .value = sp_str_lit("A") }));
  si_da_push(mem, loaded_lib->gated.publish, ((spn_gated_publish_t) { .source = { .path = sp_str_lit("A"), .tree = SPN_TREE_SOURCE } }));

  sp_must_eq(t, SPN_OK, spn_units_add_packages(s));

  spn_build_unit_t* builds [] = { s->units.target, s->units.metaprogram };
  sp_carr_for(builds, bt) {
    spn_pkg_unit_t* unit = si_get_pkg_unit(s, builds[bt], id);
    sp_must(t, unit);
    sp_expect_eq(t, (u32)1, (u32)si_da_size(unit->info->define));
    sp_expect_eq(t, (u32)1, (u32)si_da_size(unit->info->system_deps));
    sp_expect_eq(t, (u32)1, (u32)si_da_size(unit->info->include));
    sp_expect_eq(t, (u32)1, (u32)si_da_size(unit->info->macos.frameworks));
    sp_expect_eq(t, (u32)1, (u32)si_da_size(unit->info->publish));
    spn_target_info_t* lib = spn_pkg_get_target(unit->info, sp_str_lit("L"), SPN_TARGET_KIND_LIB);
    sp_must(t, lib);
    sp_expect_eq(t, (u32)1, (u32)si_da_size(lib->define));
    sp_expect_eq(t, (u32)1, (u32)si_da_size(lib->macos.frameworks));
    sp_expect_eq(t, (u32)1, (u32)si_da_size(lib->publish));
  }

  sp_expect_eq(t, (u32)0, (u32)si_da_size(loaded->define));
  sp_expect_eq(t, (u32)0, (u32)si_da_size(loaded->system_deps));
  sp_expect_eq(t, (u32)0, (u32)si_da_size(loaded->include));
  sp_expect_eq(t, (u32)0, (u32)si_da_size(loaded->macos.frameworks));
  sp_expect_eq(t, (u32)0, (u32)si_da_size(loaded->publish));
  sp_expect_eq(t, (u32)0, (u32)si_da_size(loaded_lib->define));
  sp_expect_eq(t, (u32)0, (u32)si_da_size(loaded_lib->macos.frameworks));
  sp_expect_eq(t, (u32)0, (u32)si_da_size(loaded_lib->publish));
  return SP_OK;
}
