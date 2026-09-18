#include "unit.h"

#include "paths/paths.h"
#include "target/mutate.h"

#define OBJECTS_TEST_MAX_SOURCE 4

typedef struct {
  const c8* path;
  spn_tree_t tree;
  spn_source_kind_t kind;
} objects_source_t;

typedef struct {
  const c8* objects [OBJECTS_TEST_MAX_SOURCE];
} objects_expect_t;

typedef struct {
  const c8* name;
  const c8* files [OBJECTS_TEST_MAX_SOURCE];
  objects_source_t source [OBJECTS_TEST_MAX_SOURCE];
  objects_expect_t expect;
} objects_test_t;

static const objects_test_t tests [] = {
  {
    .name = "one_object_per_file",
    .source = { { "a.c", SPN_TREE_SOURCE }, { "b.c", SPN_TREE_SOURCE } },
    .expect = { .objects = { "object/exe/app/manifest/a.c.o", "object/exe/app/manifest/b.c.o" } },
  },
  {
    .name = "shared_tree_dedups_both_declarations",
    .source = { { "a.c", SPN_TREE_SOURCE }, { "a.c", SPN_TREE_MANIFEST } },
    .expect = { .objects = { "object/exe/app/manifest/a.c.o" } },
  },
  {
    .name = "outside_tree_files_keep_a_root_label",
    .source = { { "manifest/x.c", SPN_TREE_SOURCE }, { "/manifest/x.c", SPN_TREE_SOURCE } },
    .expect = { .objects = { "object/exe/app/manifest/manifest/x.c.o", "object/exe/app/absolute/manifest/x.c.o" } },
  },
  {
    .name = "glob_expands_in_sorted_order",
    .files = { "b.c", "a.c", "c.h" },
    .source = { { "*.c", SPN_TREE_SOURCE, SPN_SOURCE_GLOB } },
    .expect = { .objects = { "object/exe/app/manifest/a.c.o", "object/exe/app/manifest/b.c.o" } },
  },
  {
    .name = "glob_dedups_against_declared_files",
    .files = { "a.c", "b.c" },
    .source = { { "b.c", SPN_TREE_SOURCE }, { "*.c", SPN_TREE_SOURCE, SPN_SOURCE_GLOB } },
    .expect = { .objects = { "object/exe/app/manifest/b.c.o", "object/exe/app/manifest/a.c.o" } },
  },
};

sp_test_each(unit_objects, create, objects_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  unit_graph_test_t graph = { .pkgs = { { .name = "A" } } };
  spn_session_t* s = build_session(mem, &graph);

  spn_pkg_id_t id = find_pkg_id(s, &graph, "A");
  spn_loaded_pkg_t* loaded = sp_ht_getp(s->packages, id);

  sp_str_t root = sp_fs_join_path(mem, sp_test_dir(t), sp_str_lit("R"));
  sp_fs_create_dir(root);
  spn_path_t tree = spn_path_make(&spn.roots, root);
  loaded->roots = (spn_tree_roots_t) { .recipe = tree, .source = tree };
  sp_carr_for(it->files, ft) {
    if (!it->files[ft]) {
      break;
    }
    sp_fs_create_file(sp_fs_join_path(mem, root, sp_cstr_as_str(it->files[ft])));
  }

  spn_target_info_t app = { .name = sp_str_lit("app"), .kind = SPN_TARGET_KIND_EXE };
  spn_target_info_init(mem, &app);
  sp_carr_for(it->source, st) {
    if (!it->source[st].path) {
      break;
    }
    sp_da_push(app.source, ((spn_source_t) {
      .kind = it->source[st].kind,
      .path = spn_tree_path(mem, &spn.roots, loaded->roots, it->source[st].tree, sp_cstr_as_str(it->source[st].path)),
    }));
  }
  sp_str_om_insert(s->pkg->exes, app.name, app);

  sp_must_eq(t, SPN_OK, spn_units_add_packages(s));
  sp_must_eq(t, SPN_OK, spn_units_add_targets(s, SPN_UNIT_SCOPE_TARGET));

  spn_pkg_unit_t* pkg = spn_session_find_pkg_unit(s, s->units.target, id);
  sp_must(t, pkg != SP_NULLPTR);
  spn_target_unit_t* target = spn_session_find_target_in_pkg(s, pkg, sp_str_lit("app"), SPN_TARGET_KIND_EXE);
  sp_must(t, target != SP_NULLPTR);

  u32 count = 0;
  sp_carr_detect_len(it->expect.objects, count, it->expect.objects[count]);
  sp_must_eq(t, count, (u32)sp_da_size(target->objects));
  sp_for(ot, count) {
    sp_test_kv_c(t, "object", it->expect.objects[ot]);
    sp_expect(t, sp_str_ends_with(target->objects[ot]->paths.object.sub, sp_cstr_as_str(it->expect.objects[ot])));
  }

  return SP_OK;
}
