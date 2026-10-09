#include "unit/unit.h"

#include "ctx/types.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "pkg/options.h"
#include "pkg/pkg.h"
#include "session/session.h"
#include "when/when.h"

static si_da(spn_path_t) clone_path_list(sp_mem_t mem, si_da(spn_path_t) source) {
  si_da(spn_path_t) result = SP_NULLPTR;
  si_da_for(source, it) {
    si_da_push(mem, result, spn_path_copy(mem, source[it]));
  }
  return result;
}

static spn_target_info_t clone_target_info(sp_mem_t mem, spn_target_info_t* source) {
  spn_target_info_t target = *source;
  si_da_copy(mem, target.source, source->source);
  target.headers = clone_path_list(mem, source->headers);
  target.include = clone_path_list(mem, source->include);
  si_da_copy(mem, target.define, source->define);
  si_da_copy(mem, target.flags, source->flags);
  si_da_copy(mem, target.link_flags, source->link_flags);
  target.linker_script = clone_path_list(mem, source->linker_script);
  si_da_copy(mem, target.system_deps, source->system_deps);
  si_da_copy(mem, target.deps, source->deps);
  si_da_copy(mem, target.embed, source->embed);
  target.configured.include = clone_path_list(mem, source->configured.include);
  si_da_copy(mem, target.macos.frameworks, source->macos.frameworks);
  return target;
}

static void clone_target_map(spn_target_map_t* result, spn_target_map_t source, sp_mem_t mem) {
  *result = sp_zero_struct(spn_target_map_t);
  si_om_for(source, it) {
    spn_target_info_t target = clone_target_info(mem, si_om_at(source, it));
    si_om_insert(mem, *result, ((spn_target_key_t) { .name = spn_intern(target.name).id, .kind = target.kind }), target);
  }
}

// Each unit owns a clone of its package's info: configure scripts mutate it,
// and option values differ per build
static spn_pkg_info_t* clone_pkg_info(spn_session_t* s, spn_pkg_id_t id, spn_build_unit_t* build, spn_loaded_pkg_t* loaded) {
  spn_pkg_info_t* source = loaded->info;
  spn_pkg_info_t* info = sp_alloc_type(s->mem, spn_pkg_info_t);
  *info = *source;
  info->applied = false;
  sp_mem_t mem = s->mem;

  clone_target_map(&info->targets, source->targets, mem);
  info->include = clone_path_list(mem, source->include);
  info->configured.include = clone_path_list(mem, source->configured.include);
  si_da_copy(mem, info->define, source->define);
  si_da_copy(mem, info->public_define, source->public_define);
  si_da_copy(mem, info->system_deps, source->system_deps);
  si_da_copy(mem, info->macos.frameworks, source->macos.frameworks);
  si_da_copy(mem, info->publish.copy, source->publish.copy);

  spn_when_env_t env;
  spn_when_env_from_profile(mem, &build->profile, &env);
  spn_resolved_options_t* options = sp_ht_getp(s->options, id);
  if (options) {
    spn_when_env_add_options(&env, options);
  }
  spn_pkg_apply_options(mem, info, &s->ctx->roots, loaded->roots, &env);
  return info;
}

static bool pkg_has_scripts(spn_loaded_pkg_t* loaded) {
  return !si_da_empty(loaded->configure.source) || !si_da_empty(loaded->build.source);
}

static u32 kind_bits(spn_dep_kind_t kind) {
  return spn_dep_kind_bit(kind);
}

static spn_pkg_unit_t* add_unit(spn_session_t* s, spn_build_unit_t* build, spn_pkg_id_t pid, u32 kinds, sp_da(spn_pkg_unit_t*)* pending) {
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

  sp_om_insert(s->units.packages, uid, sp_zero_struct(spn_pkg_unit_t));
  spn_pkg_unit_t* unit = sp_om_back(s->units.packages);
  unit->id = uid;
  unit->build = build;
  unit->session = s;
  unit->source = loaded->source;
  unit->kinds = kinds;
  unit->info = clone_pkg_info(s, pid, build, loaded);
  sp_da_init(s->mem, unit->deps);
  sp_da_init(s->mem, unit->libs);
  sp_da_init(s->mem, unit->targets);
  sp_da_init(s->mem, unit->user_nodes);
  unit->fingerprint = spn_unit_fingerprint(s, build, pid);
  spn_unit_paths_init(unit, loaded);

  sp_da_push(build->packages, unit);
  sp_da_push(*pending, unit);
  return unit;
}

static void drain(spn_session_t* s, sp_da(spn_pkg_unit_t*)* pending) {
  sp_for(it, sp_da_size(*pending)) {
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
        .unit = add_unit(s, unit->build, edge->id, kinds, pending),
        .kind = edge->kind,
        .private = edge->private,
      }));
    }
  }
  sp_da_clear(*pending);
}

spn_err_t spn_units_add_packages(spn_session_t* s) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_da(spn_pkg_unit_t*) pending = sp_da_new(scratch.mem, spn_pkg_unit_t*);

  spn_pkg_id_t root = spn_session_root_pkg(s);
  sp_da_for(s->plans.build, it) {
    add_unit(s, s->plans.build[it].build, root, kind_bits(SPN_DEP_KIND_PACKAGE) | kind_bits(SPN_DEP_KIND_TEST), &pending);
  }
  drain(s, &pending);

  sp_da(spn_pkg_unit_t*) owners = sp_da_new(scratch.mem, spn_pkg_unit_t*);
  sp_om_for(s->units.packages, it) {
    spn_pkg_unit_t* unit = sp_om_at(s->units.packages, it);
    if (unit->build == s->units.metaprogram) {
      continue;
    }
    if (pkg_has_scripts(sp_ht_getp(s->packages, unit->id.pkg))) {
      sp_da_push(owners, unit);
    }
  }

  sp_da_for(owners, it) {
    spn_resolved_pkg_t* resolved = sp_ht_getp(s->resolve, owners[it]->id.pkg);
    sp_assert(resolved);
    sp_da_for(resolved->edges, et) {
      if (resolved->edges[et].kind == SPN_DEP_KIND_BUILD) {
        add_unit(s, s->units.metaprogram, resolved->edges[et].id, kind_bits(SPN_DEP_KIND_PACKAGE) | kind_bits(SPN_DEP_KIND_BUILD), &pending);
      }
    }
  }
  drain(s, &pending);

  sp_da_for(owners, it) {
    spn_pkg_id_t id = owners[it]->id.pkg;
    if (!spn_session_find_pkg_unit(s, s->units.metaprogram, id)) {
      add_unit(s, s->units.metaprogram, id, kind_bits(SPN_DEP_KIND_BUILD), &pending);
    }
  }
  drain(s, &pending);

  sp_om_for(s->units.packages, it) {
    spn_pkg_unit_t* unit = sp_om_at(s->units.packages, it);
    if (pkg_has_scripts(sp_ht_getp(s->packages, unit->id.pkg))) {
      unit->metaprogram = spn_session_find_pkg_unit(s, s->units.metaprogram, unit->id.pkg);
      sp_assert(unit->metaprogram);
    }
  }

  sp_mem_end_scratch(scratch);
  return SPN_OK;
}
