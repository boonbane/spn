#include "spn_test.h"

#include "intern/intern.h"
#include "unit/closure/closure.h"
#include "unit/types.h"

#define CLOSURE_TEST_MAX_PKGS 8
#define CLOSURE_TEST_MAX_DEPS 8
#define CLOSURE_TEST_MAX_LIBS 4
#define CLOSURE_TEST_MAX_ENTRIES 8

typedef struct {
  const c8* name;
  bool private;
} dep_t;

typedef struct {
  const c8* name;
  spn_linkage_t kind;
  bool no_link;
  const c8* deps [CLOSURE_TEST_MAX_DEPS];
} lib_t;

typedef struct {
  const c8* name;
  spn_linkage_t kind;
  dep_t deps [CLOSURE_TEST_MAX_DEPS];
  lib_t libs [CLOSURE_TEST_MAX_LIBS];
} pkg_t;

typedef struct {
  const c8* root_deps [CLOSURE_TEST_MAX_DEPS];
  pkg_t pkgs [CLOSURE_TEST_MAX_PKGS];
} graph_t;

typedef struct {
  const c8* pkg;
  const c8* lib;
  bool private;
} entry_t;

typedef struct {
  const c8* name;
  si_closure_shared_t shared;
  si_closure_yield_t yield;
  graph_t graph;
  entry_t expect [CLOSURE_TEST_MAX_ENTRIES];
} test_t;

static const test_t tests [] = {
  {
    .name = "pkg_chain_follows_static_deps",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } } },
      { .name = "B", .kind = SPN_LIB_KIND_STATIC, .deps = { { "C" } } },
      { .name = "C", .kind = SPN_LIB_KIND_STATIC },
    },
    .expect = { { "B" }, { "C" } },
  },
  {
    .name = "pkg_walk_stops_at_shared",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } } },
      { .name = "B", .kind = SPN_LIB_KIND_SHARED, .deps = { { "C" } } },
      { .name = "C", .kind = SPN_LIB_KIND_STATIC },
    },
    .expect = { { "B" } },
  },
  {
    .name = "pkg_dedupes_diamond",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" }, { "C" } } },
      { .name = "B", .kind = SPN_LIB_KIND_STATIC, .deps = { { "D" } } },
      { .name = "C", .kind = SPN_LIB_KIND_STATIC, .deps = { { "D" } } },
      { .name = "D", .kind = SPN_LIB_KIND_STATIC },
    },
    .expect = { { "C" }, { "B" }, { "D" } },
  },
  // One shared lib makes the whole package its own link unit: none of its
  // package deps are walked, whatever its other libs are
  {
    .name = "pkg_with_a_shared_lib_stops_for_all_its_libs",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } } },
      { .name = "B", .deps = { { "C" } }, .libs = { { "L", SPN_LIB_KIND_SHARED }, { "M", SPN_LIB_KIND_STATIC } } },
      { .name = "C", .kind = SPN_LIB_KIND_STATIC },
    },
    .expect = { { "B" } },
  },
  {
    .name = "pkg_skips_unreferenced",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } } },
      { .name = "B", .kind = SPN_LIB_KIND_STATIC },
      { .name = "C", .kind = SPN_LIB_KIND_STATIC },
    },
    .expect = { { "B" } },
  },
  {
    .name = "lib_yields_every_lib_kind",
    .yield = SI_CLOSURE_YIELD_LIB,
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } } },
      {
        .name = "B",
        .kind = SPN_LIB_KIND_STATIC,
        .libs = {
          { "L", SPN_LIB_KIND_SHARED },
          { "M", SPN_LIB_KIND_OBJECT },
          { "N", SPN_LIB_KIND_SOURCE },
          { "O", SPN_LIB_KIND_STATIC, .no_link = true },
        },
      },
    },
    .expect = { { "B", "O" }, { "B", "N" }, { "B", "M" }, { "B", "L" }, { "B", "B" } },
  },
  {
    .name = "lib_order_follows_closure",
    .yield = SI_CLOSURE_YIELD_LIB,
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } } },
      { .name = "B", .kind = SPN_LIB_KIND_STATIC, .deps = { { "C" } } },
      { .name = "C", .kind = SPN_LIB_KIND_STATIC },
    },
    .expect = { { "B", "B" }, { "C", "C" } },
  },
  // Privacy inherits down the private edge: everything reached through a
  // private dep is private to the link unit
  {
    .name = "lib_carries_privacy",
    .yield = SI_CLOSURE_YIELD_LIB,
    .graph.pkgs = {
      { .name = "P", .deps = { { "B", .private = true } } },
      { .name = "B", .kind = SPN_LIB_KIND_STATIC, .deps = { { "C" } } },
      { .name = "C", .kind = SPN_LIB_KIND_STATIC },
    },
    .expect = { { "B", "B", .private = true }, { "C", "C", .private = true } },
  },
  {
    .name = "lib_leads_with_siblings",
    .yield = SI_CLOSURE_YIELD_LIB,
    .graph = {
      .root_deps = { "L" },
      .pkgs = {
        {
          .name = "P",
          .deps = { { "B" } },
          .libs = {
            { "L", SPN_LIB_KIND_STATIC, .deps = { "M" } },
            { "M", SPN_LIB_KIND_STATIC },
            { "N", SPN_LIB_KIND_STATIC, .no_link = true },
          },
        },
        { .name = "B", .kind = SPN_LIB_KIND_STATIC },
      },
    },
    .expect = { { "P", "L" }, { "P", "M" }, { "B", "B" } },
  },
  // A shared sibling is its own link unit: it appears, but its deps are its
  // own problem and are not walked into the consumer
  {
    .name = "sibling_shared_stops",
    .yield = SI_CLOSURE_YIELD_LIB,
    .graph = {
      .root_deps = { "L" },
      .pkgs = {
        { .name = "P", .libs = { { "L", SPN_LIB_KIND_SHARED, .deps = { "M" } }, { "M", SPN_LIB_KIND_STATIC } } },
      },
    },
    .expect = { { "P", "L" } },
  },
  {
    .name = "sibling_dedupes_diamond",
    .yield = SI_CLOSURE_YIELD_LIB,
    .graph = {
      .root_deps = { "L", "M" },
      .pkgs = {
        {
          .name = "P",
          .libs = {
            { "L", SPN_LIB_KIND_STATIC, .deps = { "N" } },
            { "M", SPN_LIB_KIND_STATIC, .deps = { "N" } },
            { "N", SPN_LIB_KIND_STATIC },
          },
        },
      },
    },
    .expect = { { "P", "M" }, { "P", "L" }, { "P", "N" } },
  },
  // Chasing crosses shared boundaries the link closure stops at: a shared
  // lib's own shared deps must still sit next to the executable
  {
    .name = "chase_through_shared",
    .shared = SI_CLOSURE_CHASE_SHARED,
    .yield = SI_CLOSURE_YIELD_LIB,
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } } },
      { .name = "B", .kind = SPN_LIB_KIND_SHARED, .deps = { { "C" } } },
      { .name = "C", .kind = SPN_LIB_KIND_STATIC, .deps = { { "D" } } },
      { .name = "D", .kind = SPN_LIB_KIND_SHARED },
    },
    .expect = { { "B", "B" }, { "C", "C" }, { "D", "D" } },
  },
  {
    .name = "chase_sibling_shared",
    .shared = SI_CLOSURE_CHASE_SHARED,
    .yield = SI_CLOSURE_YIELD_LIB,
    .graph = {
      .root_deps = { "L" },
      .pkgs = {
        { .name = "P", .libs = { { "L", SPN_LIB_KIND_SHARED, .deps = { "M" } }, { "M", SPN_LIB_KIND_STATIC } } },
      },
    },
    .expect = { { "P", "L" }, { "P", "M" } },
  },
};

typedef struct {
  sp_mem_t mem;
  spn_pkg_unit_t* pkgs [CLOSURE_TEST_MAX_PKGS];
  u32 count;
  spn_target_unit_t* root;
} built_t;

static spn_pkg_unit_t* find_pkg(built_t* g, const c8* name) {
  sp_intern_id_t needle = spn_intern_cstr(name).id;
  sp_for(it, g->count) {
    if (g->pkgs[it]->pkg.manifest->name.id == needle) {
      return g->pkgs[it];
    }
  }
  return SP_NULLPTR;
}

static spn_target_unit_t* find_lib(built_t* g, const c8* name) {
  sp_intern_id_t needle = spn_intern_cstr(name).id;
  sp_for(it, g->count) {
    si_da_for_value(g->pkgs[it]->libs, lib) {
      if (lib->target->info->name.id == needle) {
        return lib;
      }
    }
  }
  return SP_NULLPTR;
}

static si_target_t* make_target(sp_mem_t mem, sp_intern_str_t name, spn_target_kind_t kind, bool no_link) {
  si_target_info_t* decl = sp_alloc_type(mem, si_target_info_t);
  decl->id = (si_target_id_t) { .name = name.id, .kind = kind };
  decl->name = name;
  decl->kind = kind;
  decl->no_link = no_link;

  si_target_t* eval = sp_alloc_type(mem, si_target_t);
  eval->info = decl;
  return eval;
}

static spn_target_unit_t* add_lib(sp_mem_t mem, spn_pkg_unit_t* pkg, const c8* name, spn_linkage_t kind, bool no_link) {
  spn_target_unit_t* lib = sp_alloc_type(mem, spn_target_unit_t);
  lib->target = make_target(mem, spn_intern_cstr(name), SPN_TARGET_KIND_LIB, no_link);
  lib->pkg = pkg;
  lib->lib_kind = kind;
  sp_da_init(mem, lib->deps);
  si_da_push(mem, pkg->libs, lib);
  return lib;
}

static spn_pkg_unit_t* build_pkg(sp_mem_t mem, u32 id, const pkg_t* spec) {
  si_pkg_info_t* manifest = sp_alloc_type(mem, si_pkg_info_t);
  manifest->name = spn_intern_cstr(spec->name);

  spn_pkg_unit_t* pkg = sp_alloc_type(mem, spn_pkg_unit_t);
  pkg->id.pkg.qualified = id;
  pkg->pkg.manifest = manifest;

  if (spec->kind != SPN_LIB_KIND_NONE) {
    add_lib(mem, pkg, spec->name, spec->kind, false);
  }
  sp_carr_for(spec->libs, it) {
    if (!spec->libs[it].name) {
      break;
    }
    add_lib(mem, pkg, spec->libs[it].name, spec->libs[it].kind, spec->libs[it].no_link);
  }
  return pkg;
}

static built_t build_graph(sp_mem_t mem, const graph_t* spec) {
  built_t g = { .mem = mem };
  sp_carr_for(spec->pkgs, it) {
    if (!spec->pkgs[it].name) {
      break;
    }
    g.pkgs[it] = build_pkg(mem, it + 1, &spec->pkgs[it]);
    g.count++;
  }

  sp_for(it, g.count) {
    const pkg_t* pkg = &spec->pkgs[it];
    sp_da(spn_pkg_edge_t) deps = sp_da_new(mem, spn_pkg_edge_t);
    sp_carr_for(pkg->deps, d) {
      if (!pkg->deps[d].name) {
        break;
      }
      sp_da_push(deps, ((spn_pkg_edge_t) { .pkg = find_pkg(&g, pkg->deps[d].name), .private = pkg->deps[d].private }));
    }
    g.pkgs[it]->deps = deps;

    sp_carr_for(pkg->libs, lt) {
      if (!pkg->libs[lt].name) {
        break;
      }
      spn_target_unit_t* lib = find_lib(&g, pkg->libs[lt].name);
      sp_carr_for(pkg->libs[lt].deps, d) {
        if (!pkg->libs[lt].deps[d]) {
          break;
        }
        sp_da_push(lib->deps, find_lib(&g, pkg->libs[lt].deps[d]));
      }
    }
  }

  spn_pkg_unit_t* root = g.pkgs[0];
  spn_target_unit_t* exe = sp_alloc_type(mem, spn_target_unit_t);
  exe->pkg = root;
  exe->target = make_target(mem, root->pkg.manifest->name, SPN_TARGET_KIND_EXE, false);
  exe->edges = root->deps;
  sp_da_init(mem, exe->deps);
  sp_carr_for(spec->root_deps, it) {
    if (!spec->root_deps[it]) {
      break;
    }
    sp_da_push(exe->deps, find_lib(&g, spec->root_deps[it]));
  }
  g.root = exe;
  return g;
}

sp_test_each(closure, iterate, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  built_t g = build_graph(mem, &it->graph);

  si_da(si_closure_entry_t) yielded = sp_zero;
  si_closure_it_t walk = si_closure_it(mem, it->shared);
  si_closure_it_push(&walk, g.root);
  while (si_closure_it_next(&walk)) {
    if (walk.entry.yield == it->yield) {
      si_da_push(mem, yielded, walk.entry);
    }
  }

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].pkg);
  sp_must_eq(t, expected, (u32)si_da_size(yielded));
  sp_for(row, expected) {
    const entry_t* expect = &it->expect[row];
    si_closure_entry_t* entry = &yielded[expected - row - 1];
    sp_test_kv_c(t, "pkg", expect->pkg);
    sp_expect_str_eq_c(t, entry->pkg->pkg.manifest->name.str, expect->pkg);
    if (expect->lib) {
      sp_expect_str_eq_c(t, entry->lib->target->info->name.str, expect->lib);
    }
    sp_expect_eq(t, expect->private, entry->private);
  }
  return SP_OK;
}
