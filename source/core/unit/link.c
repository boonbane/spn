#include "unit/unit.h"

#include "core/types.h"
#include "hash_table/hash_table.h"
#include "unit/types.h"
#include "target/types.h"

typedef si_ht(spn_pkg_unit_t*, u8) pkg_set_t;
typedef si_ht(spn_target_unit_t*, u8) target_set_t;

typedef struct {
  sp_mem_t mem;
  pkg_set_t visited;
  si_da(spn_closure_entry_t) closure;
} search_t;

typedef struct {
  sp_mem_t mem;
  target_set_t visited;
  si_da(spn_target_unit_t*) order;
} sibling_search_t;

typedef struct {
  sp_mem_t mem;
  target_set_t visited;
  target_set_t unique;
  si_da(spn_target_unit_t*) libs;
} runtime_search_t;

static bool pkg_is_shared_boundary(spn_pkg_unit_t* pkg) {
  si_da_for(pkg->libs, it) {
    if (pkg->libs[it]->lib_kind == SPN_LIB_KIND_SHARED) return true;
  }
  return false;
}

static bool pkg_links_code(spn_pkg_unit_t* pkg) {
  si_da_for(pkg->libs, it) {
    spn_target_unit_t* lib = pkg->libs[it];
    if (lib->eval->info->no_link) continue;
    if (lib->lib_kind == SPN_LIB_KIND_SHARED) continue;
    return true;
  }
  return si_da_empty(pkg->libs);
}

bool spn_dep_kind_applies(spn_dep_kind_t dep, spn_target_kind_t target) {
  bool metaprogram = target == SPN_TARGET_KIND_CONFIGURE_METAPROGRAM || target == SPN_TARGET_KIND_BUILD_METAPROGRAM;
  switch (dep) {
    case SPN_DEP_KIND_PACKAGE: return !metaprogram;
    case SPN_DEP_KIND_TEST:    return target == SPN_TARGET_KIND_TEST;
    case SPN_DEP_KIND_BUILD:   return target == SPN_TARGET_KIND_BUILD_METAPROGRAM;
  }
  sp_unreachable_return(false);
}

static bool edge_links(spn_pkg_dep_t* dep, bool tests, bool builds) {
  switch (dep->kind) {
    case SPN_DEP_KIND_PACKAGE: return !builds;
    case SPN_DEP_KIND_TEST:    return tests;
    case SPN_DEP_KIND_BUILD:   return builds;
  }
  sp_unreachable_return(false);
}

#define CLOSURE_SEARCH_PRIVATE true
#define CLOSURE_SEARCH_PUBLIC false

#define CLOSURE_SEARCH_TESTS true
#define CLOSURE_DO_NOT_SEARCH_TESTS false

#define CLOSURE_SEARCH_BUILDS true
#define CLOSURE_DO_NOT_SEARCH_BUILDS false

static void collect(search_t* s, spn_pkg_unit_t* pkg, bool private, bool tests, bool builds) {
  sp_da(spn_pkg_dep_t) deps = pkg->deps;
  sp_da_for(deps, it) {
    spn_pkg_dep_t* dep = &deps[it];
    if (!edge_links(dep, tests, builds)) {
      continue;
    }
    if (si_ht_getp(s->visited, dep->unit)) {
      continue;
    }
    si_ht_insert(s->mem, s->visited, dep->unit, (u8)true);

    // A shared lib is its own link unit; it already resolved its dependencies,
    // so the consumer stops here instead of inheriting its private closure.
    if (!pkg_is_shared_boundary(dep->unit)) {

      collect(s, dep->unit, private || dep->private, CLOSURE_DO_NOT_SEARCH_TESTS, CLOSURE_DO_NOT_SEARCH_BUILDS);
    }

    si_da_push(s->mem, s->closure, ((spn_closure_entry_t) {
      .pkg = dep->unit,
      .private = private || dep->private,
    }));
  }
}

static bool sibling_code_links(spn_target_unit_t* dep) {
  if (dep->eval->info->no_link) {
    return false;
  }
  switch (dep->lib_kind) {
    case SPN_LIB_KIND_STATIC:
    case SPN_LIB_KIND_SOURCE:
    case SPN_LIB_KIND_OBJECT: {
      return true;
    }
    case SPN_LIB_KIND_SHARED:
    case SPN_LIB_KIND_NONE: {
      return false;
    }
  }
  sp_unreachable_return(false);
}

static void collect_siblings(sibling_search_t* s, spn_target_unit_t* target) {
  sp_da_for(target->deps, it) {
    spn_target_unit_t* dep = target->deps[it];
    if (si_ht_getp(s->visited, dep)) {
      continue;
    }
    si_ht_insert(s->mem, s->visited, dep, (u8)true);

    if (sibling_code_links(dep)) {
      collect_siblings(s, dep);
    }

    si_da_push(s->mem, s->order, dep);
  }
}

static si_da(spn_target_unit_t*) sibling_link_targets(sp_mem_t mem, spn_target_unit_t* root) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);

  sibling_search_t search = {
    .mem = s.mem,
  };
  si_ht_insert(s.mem, search.visited, root, (u8)true);
  collect_siblings(&search, root);

  si_da(spn_target_unit_t*) targets = sp_zero;
  si_da_rfor(search.order, it) {
    si_da_push(mem, targets, search.order[it]);
  }
  sp_mem_end_scratch(s);
  return targets;
}

si_da(spn_closure_entry_t) si_link_get_target_closure(sp_mem_t mem, spn_target_unit_t* root) {
  si_da(spn_closure_entry_t) closure = sp_zero;
  si_da_push(mem, closure, ((spn_closure_entry_t) {
    .pkg = root->pkg,
    .targets = sibling_link_targets(mem, root),
    .links_code = true,
  }));

  if (root->eval->info->kind == SPN_TARGET_KIND_CONFIGURE_METAPROGRAM) {
    return closure;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);

  search_t search = {
    .mem = s.mem,
  };
  si_ht_insert(s.mem, search.visited, root->pkg, (u8)true);
  collect(&search, root->pkg, CLOSURE_SEARCH_PUBLIC, root->eval->info->kind == SPN_TARGET_KIND_TEST, root->eval->info->kind == SPN_TARGET_KIND_BUILD_METAPROGRAM);

  // The result is in post-order. Reversing us gives us a topological sort.
  si_da_rfor(search.closure, it) {
    sp_assert(search.closure[it].pkg);
    si_da_push(mem, closure, ((spn_closure_entry_t) {
      .pkg = search.closure[it].pkg,
      .targets = search.closure[it].pkg->libs,
      .private = search.closure[it].private,
      .links_code = pkg_links_code(search.closure[it].pkg),
    }));
  }
  sp_mem_end_scratch(s);
  return closure;
}

static void collect_runtime(search_t* s, spn_pkg_unit_t* pkg, bool tests, bool builds) {
  sp_da(spn_pkg_dep_t) deps = pkg->deps;
  sp_da_for(deps, it) {
    spn_pkg_dep_t* dep = &deps[it];
    if (!edge_links(dep, tests, builds)) {
      continue;
    }
    if (si_ht_getp(s->visited, dep->unit)) {
      continue;
    }
    si_ht_insert(s->mem, s->visited, dep->unit, (u8)true);

    collect_runtime(s, dep->unit, CLOSURE_DO_NOT_SEARCH_TESTS, CLOSURE_DO_NOT_SEARCH_BUILDS);

    si_da_push(s->mem, s->closure, ((spn_closure_entry_t) {
      .pkg = dep->unit,
    }));
  }
}

static void push_unique_runtime_lib(runtime_search_t* s, spn_target_unit_t* lib) {
  if (lib->eval->info->no_link) return;
  if (lib->lib_kind != SPN_LIB_KIND_SHARED) return;
  if (si_ht_getp(s->unique, lib)) return;
  si_ht_insert(s->mem, s->unique, lib, (u8)true);
  si_da_push(s->mem, s->libs, lib);
}

static void collect_runtime_siblings(runtime_search_t* s, spn_target_unit_t* target) {
  sp_da_for(target->deps, it) {
    spn_target_unit_t* dep = target->deps[it];
    if (si_ht_getp(s->visited, dep)) continue;
    si_ht_insert(s->mem, s->visited, dep, (u8)true);

    push_unique_runtime_lib(s, dep);
    collect_runtime_siblings(s, dep);
  }
}

si_da(spn_target_unit_t*) si_link_get_target_runtime_libs(sp_mem_t mem, spn_target_unit_t* root) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);

  search_t search = {
    .mem = s.mem,
  };
  si_ht_insert(s.mem, search.visited, root->pkg, (u8)true);
  collect_runtime(&search, root->pkg, root->eval->info->kind == SPN_TARGET_KIND_TEST, root->eval->info->kind == SPN_TARGET_KIND_BUILD_METAPROGRAM);

  runtime_search_t runtime = {
    .mem = s.mem,
  };
  collect_runtime_siblings(&runtime, root);
  si_da_for(search.closure, it) {
    si_da_for(search.closure[it].pkg->libs, lt) {
      push_unique_runtime_lib(&runtime, search.closure[it].pkg->libs[lt]);
    }
  }

  si_da(spn_target_unit_t*) libs = sp_zero;
  si_da_copy(mem, libs, runtime.libs);
  sp_mem_end_scratch(s);
  return libs;
}

si_da(spn_link_lib_t) si_link_get_closure_libs(sp_mem_t mem, si_da(spn_closure_entry_t) closure) {
  si_da(spn_link_lib_t) libs = sp_zero;

  si_da_for(closure, it) {
    spn_closure_entry_t* entry = &closure[it];
    si_da_for(entry->targets, lt) {
      spn_target_unit_t* lib = entry->targets[lt];
      if (lib->eval->info->no_link) {
        continue;
      }

      switch (lib->lib_kind) {
        case SPN_LIB_KIND_STATIC:
        case SPN_LIB_KIND_SHARED: {
          si_da_push(mem, libs, ((spn_link_lib_t) {
            .pkg = entry->pkg,
            .lib = lib,
            .private = entry->private,
          }));
          break;
        }
        case SPN_LIB_KIND_SOURCE:
        case SPN_LIB_KIND_OBJECT:
        case SPN_LIB_KIND_NONE: {
          break;
        }
      }
    }
  }

  return libs;
}
