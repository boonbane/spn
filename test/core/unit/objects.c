#include "unit.h"

#include "paths/paths.h"

#define OBJECTS_TEST_MAX_SOURCE 4

typedef struct {
  const c8* path;
  spn_tree_t tree;
  spn_source_kind_t kind;
} source_t;

typedef struct {
  const c8* name;
  const c8* files [OBJECTS_TEST_MAX_SOURCE];
  source_t source [OBJECTS_TEST_MAX_SOURCE];
  const c8* expect [OBJECTS_TEST_MAX_SOURCE];
} test_t;

static const test_t tests [] = {
  {
    .name = "one_object_per_file",
    .source = { { "a.c", SPN_TREE_SOURCE }, { "b.c", SPN_TREE_SOURCE } },
    .expect = { "manifest/a.c.o", "manifest/b.c.o" },
  },
  {
    .name = "shared_tree_dedups_both_declarations",
    .source = { { "a.c", SPN_TREE_SOURCE }, { "a.c", SPN_TREE_MANIFEST } },
    .expect = { "manifest/a.c.o" },
  },
  {
    .name = "outside_tree_files_keep_a_root_label",
    .source = { { "manifest/x.c", SPN_TREE_SOURCE }, { "/manifest/x.c" } },
    .expect = { "manifest/manifest/x.c.o", "absolute/manifest/x.c.o" },
  },
  {
    .name = "glob_expands_in_sorted_order",
    .files = { "b.c", "a.c", "c.h" },
    .source = { { "*.c", SPN_TREE_SOURCE, SPN_SOURCE_GLOB } },
    .expect = { "manifest/a.c.o", "manifest/b.c.o" },
  },
  {
    .name = "glob_dedups_against_declared_files",
    .files = { "a.c", "b.c" },
    .source = { { "b.c", SPN_TREE_SOURCE }, { "*.c", SPN_TREE_SOURCE, SPN_SOURCE_GLOB } },
    .expect = { "manifest/b.c.o", "manifest/a.c.o" },
  },
};

static const graph_t graph = { .pkgs = { { .name = "A" } } };

sp_test_each(unit_objects, create, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  sp_mem_t mem = sp_test_arena(t);
  units_t* u = units_new(t, &graph);
  spn_resolved_pkg_t* resolved = units_resolved(u, "A");

  sp_str_t root = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("R"));
  sp_fs_create_dir(root);
  spn_path_t tree = spn_path_make(&u->ctx.roots, root);
  resolved->roots = (spn_tree_roots_t) { .recipe = tree, .source = tree };
  sp_carr_for(it->files, ft) {
    if (!it->files[ft]) {
      break;
    }
    sp_fs_create_file(sp_fs_join_path(mem, root, sp_cstr_as_str(it->files[ft])));
  }

  test_manifest_target(mem, units_manifest(u, "A"), sp_str_lit("X"), SPN_TARGET_KIND_EXE);
  spn_pkg_unit_t* pkg = SP_NULLPTR;
  sp_must_eq(t, SPN_OK, units_add_package(u, "A", &pkg));
  si_target_t* target = spn_pkg_get_target(&pkg->pkg, sp_str_lit("X"), SPN_TARGET_KIND_EXE);
  sp_must(t, target);
  sp_carr_for(it->source, st) {
    if (!it->source[st].path) {
      break;
    }
    sp_str_t path = sp_cstr_as_str(it->source[st].path);
    si_da_push(mem, target->source, ((spn_source_t) {
      .kind = it->source[st].kind,
      .path = it->source[st].tree == SPN_TREE_NONE
        ? spn_path_make(&u->ctx.roots, path)
        : spn_tree_path(mem, &u->ctx.roots, resolved->roots, it->source[st].tree, path),
    }));
  }

  si_da(spn_target_unit_t*) targets = sp_zero;
  spn_target_unit_t* unit = SP_NULLPTR;
  sp_must_eq(t, SPN_OK, units_add_target(u, pkg, (graph_root_t) { "X", SPN_TARGET_KIND_EXE }, &unit));
  si_da_push(mem, targets, unit);
  sp_must_eq(t, SPN_OK, units_plan(u, targets));

  u32 count = 0;
  sp_carr_detect_len(it->expect, count, it->expect[count]);
  sp_must_eq(t, count, (u32)sp_da_size(unit->objects));
  sp_for(ot, count) {
    sp_test_kv_c(t, "object", it->expect[ot]);
    sp_expect_str_eq_c(t, unit->objects[ot]->name, it->expect[ot]);
  }
  return SP_OK;
}
