#include "unit/unit.h"

#include "ctx/types.h"
#include "paths/paths.h"
#include "options/options.h"
#include "pkg/pkg.h"
#include "profile/profile.h"
#include "session/session.h"
#include "when/when.h"

static u32 kind_bits(spn_dep_kind_t kind) {
  return spn_dep_kind_bit(kind);
}

static spn_pkg_unit_t* add_unit(spn_session_t* s, spn_build_unit_t* build, spn_pkg_id_t pid, u32 kinds, sp_mem_t mem, si_da(spn_pkg_unit_t*)* pending) {
  spn_pkg_unit_id_t uid = { .pkg = pid, .build = build->id };
  if (sp_om_has(s->units.packages, uid)) {
    // First creation wins, so a request may never widen an existing unit's
    // kinds: members must be created before their host fallback
    spn_pkg_unit_t* unit = sp_om_get(s->units.packages, uid);
    sp_assert((kinds & ~unit->kinds) == 0);
    return unit;
  }

  spn_loaded_pkg_t* loaded = sp_ht_getp(s->packages, pid);
  sp_assert(loaded);

  spn_when_env_t env = sp_zero;
  spn_profile_env(s->mem, &build->profile, &env);
  spn_resolved_options_t* options = sp_ht_getp(s->options, pid);
  if (options) {
    spn_options_env_set(&env, options);
  }

  sp_om_insert(s->units.packages, uid, sp_zero_struct(spn_pkg_unit_t));
  spn_pkg_unit_t* unit = sp_om_back(s->units.packages);
  unit->id = uid;
  unit->build = build;
  unit->session = s;
  unit->source = loaded->source;
  unit->kinds = kinds;
  unit->pkg = spn_pkg_eval(s->mem, loaded->manifest, &s->ctx->roots, loaded->roots, &env);
  sp_da_init(s->mem, unit->deps);
  sp_da_init(s->mem, unit->targets);
  sp_da_init(s->mem, unit->user_nodes);
  unit->fingerprint = spn_unit_fingerprint(s, build, pid);
  spn_unit_paths_init(unit, loaded);

  sp_da_push(build->packages, unit);
  si_da_push(mem, *pending, unit);
  return unit;
}

static void drain(spn_session_t* s, sp_mem_t mem, si_da(spn_pkg_unit_t*)* pending) {
  sp_for(it, si_da_size(*pending)) {
    spn_pkg_unit_t* unit = (*pending)[it];
    spn_resolved_pkg_t* resolved = sp_ht_getp(s->resolve, unit->id.pkg);
    sp_assert(resolved);

    u32 kinds = unit->build == s->units.metaprogram
      ? kind_bits(SPN_DEP_KIND_PACKAGE) | kind_bits(SPN_DEP_KIND_BUILD)
      : kind_bits(SPN_DEP_KIND_PACKAGE);

    sp_da_for(resolved->edges, et) {
      spn_resolved_dep_t* edge = &resolved->edges[et];
      if (!(kind_bits(edge->kind) & unit->kinds)) {
        continue;
      }
      sp_da_push(unit->deps, ((spn_pkg_dep_t) {
        .unit = add_unit(s, unit->build, edge->id, kinds, mem, pending),
        .kind = edge->kind,
        .private = edge->private,
      }));
    }
  }
  si_da_clear(*pending);
}

spn_err_t spn_units_add_packages(spn_session_t* s) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  si_da(spn_pkg_unit_t*) pending = sp_zero;

  spn_pkg_id_t root = si_get_root_pkg(s);
  sp_da_for(s->plans.build, it) {
    spn_build_plan_t* plan = &s->plans.build[it];
    plan->root = add_unit(s, plan->build, root, kind_bits(SPN_DEP_KIND_PACKAGE) | kind_bits(SPN_DEP_KIND_TEST), scratch.mem, &pending);
  }
  drain(s, scratch.mem, &pending);

  si_da(spn_pkg_unit_t*) owners = sp_zero;
  sp_om_for(s->units.packages, it) {
    spn_pkg_unit_t* unit = sp_om_at(s->units.packages, it);
    if (unit->build == s->units.metaprogram) {
      continue;
    }
    if (spn_pkg_has_scripts(&unit->pkg)) {
      si_da_push(scratch.mem, owners, unit);
    }
  }

  si_da_for(owners, it) {
    spn_resolved_pkg_t* resolved = sp_ht_getp(s->resolve, owners[it]->id.pkg);
    sp_assert(resolved);
    sp_da_for(resolved->edges, et) {
      if (resolved->edges[et].kind == SPN_DEP_KIND_BUILD) {
        add_unit(s, s->units.metaprogram, resolved->edges[et].id, kind_bits(SPN_DEP_KIND_PACKAGE) | kind_bits(SPN_DEP_KIND_BUILD), scratch.mem, &pending);
      }
    }
  }
  drain(s, scratch.mem, &pending);

  si_da_for(owners, it) {
    spn_pkg_id_t id = owners[it]->id.pkg;
    if (!si_get_pkg_unit(s, s->units.metaprogram, id)) {
      add_unit(s, s->units.metaprogram, id, kind_bits(SPN_DEP_KIND_BUILD), scratch.mem, &pending);
    }
  }
  drain(s, scratch.mem, &pending);

  sp_om_for(s->units.packages, it) {
    spn_pkg_unit_t* unit = sp_om_at(s->units.packages, it);
    if (spn_pkg_has_scripts(&unit->pkg)) {
      unit->metaprogram = si_get_pkg_unit(s, s->units.metaprogram, unit->id.pkg);
      sp_assert(unit->metaprogram);
    }
  }

  sp_mem_end_scratch(scratch);
  return SPN_OK;
}
