#include "unit/unit.h"

#include "hash_table/hash_table.h"
#include "pkg/id.h"

typedef struct {
  spn_session_t* s;
  sp_mem_t mem;
  spn_closure_t* closure;
  si_ht(spn_target_unit_t*, u8) targets;
  si_ht(spn_pkg_unit_t*, u8) pkgs;
  si_ht(spn_pkg_unit_t*, u8) deps;
} walk_t;

static spn_err_t visit_target(walk_t* w, spn_target_unit_t* target);

static void visit_pkg(walk_t* w, spn_pkg_unit_t* pkg) {
  if (si_ht_getp(w->pkgs, pkg)) {
    return;
  }
  si_ht_insert(w->mem, w->pkgs, pkg, (u8)true);
  si_da_push(w->s->mem, w->closure->pkgs, pkg);
}

static spn_pkg_unit_t* follow(spn_session_t* s, spn_pkg_unit_t* pkg, spn_resolved_dep_t* edge) {
  si_da_for_ptr(pkg->deps, dep) {
    if (dep->kind == edge->kind && spn_pkg_id_eq(dep->unit->id.pkg, edge->id)) {
      return dep->unit;
    }
  }
  spn_pkg_unit_t* unit = spn_pkg_unit_ensure(s, pkg->build, edge->id);
  si_da_push(s->mem, pkg->deps, ((spn_pkg_dep_t) {
    .unit = unit,
    .kind = edge->kind,
    .private = edge->private,
  }));
  return unit;
}

static spn_err_t visit_dep(walk_t* w, spn_pkg_unit_t* pkg) {
  if (si_ht_getp(w->deps, pkg)) {
    return SPN_OK;
  }
  si_ht_insert(w->mem, w->deps, pkg, (u8)true);
  visit_pkg(w, pkg);

  si_om_for_each(pkg->pkg.targets, target) {
    if (target->info->kind != SPN_TARGET_KIND_LIB) {
      continue;
    }
    spn_target_unit_t* lib = SP_NULLPTR;
    spn_try(spn_target_unit_ensure(w->s, pkg, target, &lib));
    spn_try(visit_target(w, lib));
  }

  spn_resolved_pkg_t* resolved = sp_ht_getp(w->s->resolve, pkg->id.pkg);
  sp_da_for_ptr(resolved->edges, edge) {
    if (edge->kind == SPN_DEP_KIND_PACKAGE) {
      spn_try(visit_dep(w, follow(w->s, pkg, edge)));
    }
  }
  return SPN_OK;
}

static spn_err_t visit_target(walk_t* w, spn_target_unit_t* target) {
  if (si_ht_getp(w->targets, target)) {
    return SPN_OK;
  }
  si_ht_insert(w->mem, w->targets, target, (u8)true);
  si_da_push(w->s->mem, w->closure->targets, target);

  spn_pkg_unit_t* pkg = target->pkg;
  visit_pkg(w, pkg);
  si_da_for_value(target->deps, dep) {
    spn_try(visit_target(w, dep));
  }

  spn_resolved_pkg_t* resolved = sp_ht_getp(w->s->resolve, pkg->id.pkg);
  sp_da_for_ptr(resolved->edges, edge) {
    if (spn_dep_kind_applies(edge->kind, target->target->info->kind)) {
      spn_try(visit_dep(w, follow(w->s, pkg, edge)));
    }
  }
  return SPN_OK;
}

static spn_err_t closure_new(spn_session_t* s, sp_mem_t scratch, spn_pkg_unit_t* root, si_da(spn_target_unit_t*) roots, spn_closure_t* closure) {
  *closure = (spn_closure_t) {
    .root = root,
    .roots = roots,
  };
  walk_t w = {
    .s = s,
    .mem = scratch,
    .closure = closure,
  };
  visit_pkg(&w, root);
  si_da_for_value(roots, target) {
    spn_try(visit_target(&w, target));
  }
  return SPN_OK;
}

spn_err_t spn_closure_new(spn_session_t* s, spn_pkg_unit_t* root, si_da(spn_target_unit_t*) roots, spn_closure_t* closure) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = closure_new(s, scratch.mem, root, roots, closure);
  sp_mem_end_scratch(scratch);
  return err;
}
