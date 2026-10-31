#include "unit/unit.h"

#include "hash_table/hash_table.h"

typedef struct {
  sp_mem_t mem;
  sp_mem_t scratch;
  spn_closure_t* closure;
  si_ht(spn_target_unit_t*, u8) targets;
  si_ht(spn_pkg_unit_t*, u8) pkgs;
  si_ht(spn_pkg_unit_t*, u8) walked;
} walk_t;

static void visit_target(walk_t* w, spn_target_unit_t* target);

static void visit_pkg(walk_t* w, spn_pkg_unit_t* pkg) {
  if (si_ht_getp(w->pkgs, pkg)) {
    return;
  }
  si_ht_insert(w->scratch, w->pkgs, pkg, (u8)true);
  si_da_push(w->mem, w->closure->pkgs, pkg);
}

static void visit_dep(walk_t* w, spn_pkg_unit_t* pkg) {
  if (si_ht_getp(w->walked, pkg)) {
    return;
  }
  si_ht_insert(w->scratch, w->walked, pkg, (u8)true);
  visit_pkg(w, pkg);
  si_da_for_value(pkg->libs, lib) {
    visit_target(w, lib);
  }
  si_da_for_ptr(pkg->deps, edge) {
    visit_dep(w, edge->pkg);
  }
}

static void visit_target(walk_t* w, spn_target_unit_t* target) {
  if (si_ht_getp(w->targets, target)) {
    return;
  }
  si_ht_insert(w->scratch, w->targets, target, (u8)true);
  si_da_push(w->mem, w->closure->targets, target);
  visit_pkg(w, target->pkg);
  si_da_for_ptr(target->edges, edge) {
    visit_dep(w, edge->pkg);
  }
  si_da_for_value(target->deps, dep) {
    visit_target(w, dep);
  }
}

spn_closure_t spn_closure_collect(sp_mem_t mem, spn_pkg_unit_t* root, si_da(spn_target_unit_t*) roots) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  spn_closure_t closure = { .root = root, .roots = roots };
  walk_t w = { .mem = mem, .scratch = scratch.mem, .closure = &closure };
  visit_pkg(&w, root);
  si_ht_insert(w.scratch, w.walked, root, (u8)true);
  si_da_for_value(roots, target) {
    visit_target(&w, target);
  }
  sp_mem_end_scratch(scratch);
  return closure;
}
