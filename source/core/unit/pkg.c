#include "unit/unit.h"

#include "ctx/types.h"
#include "enum/enum.h"
#include "error/error.h"
#include "options/options.h"
#include "paths/paths.h"
#include "pkg/pkg.h"
#include "profile/profile.h"
#include "target/select.h"
#include "when/when.h"

static si_pkg_t evaluate(spn_units_t* u, spn_build_unit_t* build, const spn_resolved_pkg_t* resolved, spn_tree_roots_t trees) {
  sp_mem_t mem = u->mem;
  spn_when_env_t env = sp_zero;
  spn_profile_env(mem, &build->profile, &env);
  spn_options_env_set(&env, &resolved->options.values);
  return si_evaluate_package(mem, resolved->manifest, &u->ctx->roots, trees, &env);
}

typedef struct {
  u32 take;
  bool published;
  bool self;
  bool host;
} consumes_t;

static const consumes_t consumes [] = {
  [SPN_TARGET_KIND_LIB] = { .take = 1u << SPN_DEP_KIND_PACKAGE, .published = true, .host = true },
  [SPN_TARGET_KIND_EXE] = { .take = 1u << SPN_DEP_KIND_PACKAGE, .published = true, .host = true },
  [SPN_TARGET_KIND_TOOL] = { .take = 1u << SPN_DEP_KIND_TOOL, .published = true, .host = true },
  [SPN_TARGET_KIND_TEST] = { .take = (1u << SPN_DEP_KIND_PACKAGE) | (1u << SPN_DEP_KIND_TEST), .published = true, .host = true },
  [SPN_TARGET_KIND_EXAMPLE] = { .take = 1u << SPN_DEP_KIND_PACKAGE, .self = true, .host = true },
  [SPN_TARGET_KIND_BUILD_METAPROGRAM] = { .take = 1u << SPN_DEP_KIND_BUILD },
  [SPN_TARGET_KIND_CONFIGURE_METAPROGRAM] = sp_zero,
};

static void append(sp_mem_t mem, si_da(spn_requirement_t)* list, si_da(spn_requirement_t) values) {
  si_da_for_value(values, value) {
    si_da_push(mem, *list, value);
  }
}

static void push_include(sp_mem_t mem, spn_target_unit_t* unit, spn_pkg_unit_t* pkg) {
  si_da_for_value(unit->includes, included) {
    if (included == pkg) {
      return;
    }
  }
  si_da_push(mem, unit->includes, pkg);
}

static si_target_t* get_script(si_pkg_t* pkg, spn_target_kind_t kind) {
  si_om_for_each(pkg->targets, target) {
    if (target->info->kind == kind && !si_da_empty(target->source)) {
      return target;
    }
  }
  return SP_NULLPTR;
}

static void init_package_unit(spn_units_t* u, spn_build_unit_t* build, const spn_resolved_pkg_t* resolved, spn_pkg_unit_t* unit) {
  sp_mem_t mem = u->mem;
  unit->id = (spn_pkg_unit_id_t) { .pkg = resolved->id, .build = build->id };
  unit->build = build;
  unit->units = u;
  unit->resolved = resolved;
  if (resolved->origin.source.kind == SPN_PKG_ROOT_GIT) {
    unit->pin = (spn_source_pin_t) {
      .kind = SPN_PKG_ROOT_GIT,
      .rev = resolved->origin.source.git.rev,
      .dir = resolved->origin.source.git.dir,
      .patches = resolved->origin.source.git.patches.hash,
    };
  }
  sp_da_init(mem, unit->user_nodes);
  unit->fingerprint = spn_unit_fingerprint(u, build, resolved);

  switch (resolved->source) {
    case SPN_PKG_SOURCE_ROOT:
    case SPN_PKG_SOURCE_FILE: {
      spn_path_t work = spn_path_join(mem, build->paths.root, sp_str_lit(".spn"));
      spn_path_t store = spn_path_join(mem, build->paths.root, sp_str_lit("store"));
      unit->paths.work = spn_path_join(mem, work, resolved->manifest->name.str);
      unit->paths.store = spn_path_join(mem, store, resolved->manifest->name.str);
      break;
    }
    case SPN_PKG_SOURCE_INDEX: {
      sp_str_t fingerprint = spn_unit_fingerprint_str(mem, unit->fingerprint);
      spn_path_t work = spn_path_s(mem, SPN_DIR_ID_WORK, resolved->manifest->name.str);
      spn_path_t store = spn_path_s(mem, SPN_DIR_ID_PKG, resolved->manifest->name.str);
      unit->paths.work = spn_path_join(mem, work, fingerprint);
      unit->paths.store = spn_path_join(mem, store, fingerprint);
      break;
    }
  }
  unit->paths.work = spn_path_anchor(mem, &u->ctx->roots, unit->paths.work);
  unit->paths.store = spn_path_anchor(mem, &u->ctx->roots, unit->paths.store);
  unit->paths.include = spn_path_join(mem, unit->paths.store, SP_LIT("include"));
  unit->paths.share = spn_path_join(mem, unit->paths.store, SP_LIT("share"));
  unit->paths.bin = spn_path_join(mem, unit->paths.store, SP_LIT("bin"));
  unit->paths.lib = spn_path_join(mem, unit->paths.store, SP_LIT("lib"));
  unit->paths.roots = (spn_tree_roots_t) {
    .recipe = resolved->roots.recipe,
    .source = resolved->roots.source,
    .work = unit->paths.work,
  };
  unit->pkg = evaluate(u, build, resolved, unit->paths.roots);
  si_da_for_ptr(unit->pkg.publish.entries, entry) {
    unit->publishes.include = unit->publishes.include || entry->root == SPN_PUBLISH_ROOT_INCLUDE;
  }
  si_om_for_each(unit->pkg.targets, target) {
    si_da_for_ptr(target->publish, entry) {
      unit->publishes.include = unit->publishes.include || entry->root == SPN_PUBLISH_ROOT_INCLUDE;
    }
  }
}

static spn_err_t add_script(spn_units_t* u, const spn_resolved_pkg_t* resolved, spn_target_kind_t kind, spn_target_unit_t** result) {
  if (!sp_om_has(u->scripts, resolved->id)) {
    sp_om_insert(u->scripts, resolved->id, sp_zero_struct(spn_pkg_unit_t));
    init_package_unit(u, u->metabuild, resolved, sp_om_back(u->scripts));
  }
  spn_pkg_unit_t* pkg = sp_om_get(u->scripts, resolved->id);
  return si_add_target_unit(u, pkg, get_script(&pkg->pkg, kind), result);
}

spn_err_t si_add_package_unit(spn_units_t* u, spn_build_unit_t* build, const spn_resolved_pkg_t* resolved, spn_pkg_unit_t** result) {
  spn_pkg_unit_id_t uid = { .pkg = resolved->id, .build = build->id };
  if (sp_om_has(u->packages, uid)) {
    *result = sp_om_get(u->packages, uid);
    return SPN_OK;
  }

  sp_om_insert(u->packages, uid, sp_zero_struct(spn_pkg_unit_t));
  spn_pkg_unit_t* unit = sp_om_back(u->packages);
  init_package_unit(u, build, resolved, unit);
  *result = unit;

  si_pkg_t meta = evaluate(u, u->metabuild, resolved, unit->paths.roots);
  if (get_script(&meta, SPN_TARGET_KIND_CONFIGURE_METAPROGRAM)) {
    spn_try(add_script(u->configure, resolved, SPN_TARGET_KIND_CONFIGURE_METAPROGRAM, &unit->reactor));
  }
  if (get_script(&meta, SPN_TARGET_KIND_BUILD_METAPROGRAM)) {
    spn_try(add_script(u, resolved, SPN_TARGET_KIND_BUILD_METAPROGRAM, &unit->metaprogram));
  }
  return SPN_OK;
}

spn_err_t si_add_package_unit_closure(spn_units_t* u, spn_build_unit_t* build, const spn_resolved_pkg_t* resolved, spn_pkg_unit_t** result) {
  spn_pkg_unit_t* pkg = SP_NULLPTR;
  spn_try(si_add_package_unit(u, build, resolved, &pkg));
  *result = pkg;
  if (pkg->expanded) {
    return SPN_OK;
  }
  pkg->expanded = true;

  sp_da_for_ptr(pkg->resolved->edges, edge) {
    if (edge->kind != SPN_DEP_KIND_PACKAGE) {
      continue;
    }
    spn_pkg_unit_t* dep = SP_NULLPTR;
    spn_try(si_add_package_unit_closure(u, build, sp_ht_getp(u->resolve, edge->id), &dep));
    si_da_push(u->mem, pkg->deps, ((spn_pkg_edge_t) { .pkg = dep, .public = edge->public }));
  }

  si_om_for_each(pkg->pkg.targets, target) {
    if (target->info->kind != SPN_TARGET_KIND_LIB) {
      continue;
    }
    spn_target_unit_t* lib = SP_NULLPTR;
    spn_try(si_add_target_unit(u, pkg, target, &lib));
    si_da_push(u->mem, pkg->libs, lib);
  }
  return SPN_OK;
}

spn_err_t si_add_target_unit(spn_units_t* u, spn_pkg_unit_t* pkg, si_target_t* target, spn_target_unit_t** result) {
  spn_target_unit_id_t id = {
    .pkg = pkg->id,
    .target = target->info->id,
  };
  if (sp_om_has(u->targets, id)) {
    *result = sp_om_get(u->targets, id);
    return SPN_OK;
  }

  sp_om_insert(u->targets, id, sp_zero_s(spn_target_unit_t));
  spn_target_unit_t* unit = sp_om_back(u->targets);
  unit->id = id;
  unit->pkg = pkg;
  unit->target = target;
  sp_da_init(u->mem, unit->objects);

  switch (target->info->kind) {
    case SPN_TARGET_KIND_LIB: {
      const spn_profile_info_t* profile = &pkg->build->profile;
      const spn_pkg_config_t* config = pkg->resolved->config;
      spn_kind_query_t query = { .linkage = profile->linking.linkage };
      if (config && !sp_opt_is_null(config->kind)) {
        sp_opt_set(query.config, config->kind.value);
      }

      if (spn_target_select_lib_kind(target->info->linkages, query, &unit->lib_kind)) {
        spn_linkage_requester_t requester = SPN_LINKAGE_REQUESTER_TARGET;
        if (profile->request.linkage) {
          requester = SPN_LINKAGE_REQUESTER_PROFILE;
        }
        else if (profile->request.libc == SPN_RUNTIME_STATIC) {
          requester = SPN_LINKAGE_REQUESTER_LIBC;
        }
        sp_da(spn_linkage_t) supported = sp_da_new(u->ctx->events->mem, spn_linkage_t);
        if (target->info->linkages.shared) {
          sp_da_push(supported, SPN_LIB_KIND_SHARED);
        }
        if (target->info->linkages.static_lib) {
          sp_da_push(supported, SPN_LIB_KIND_STATIC);
        }
        return spn_err_emit(u->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_TARGET_LINKAGE,
          .target = {
            .pkg = pkg->pkg.manifest->name.str,
            .name = target->info->name.str,
            .requested = spn_linkage_to_str(query.config.some ? query.config.value : query.linkage),
            .requester = query.config.some ? SPN_LINKAGE_REQUESTER_ROOT_MANIFEST : requester,
            .supported = supported,
          },
        });
      }

      switch (unit->lib_kind) {
        case SPN_LIB_KIND_STATIC: unit->kind = SPN_CC_OUTPUT_STATIC_LIB; break;
        case SPN_LIB_KIND_SHARED: unit->kind = SPN_CC_OUTPUT_SHARED_LIB; break;
        case SPN_LIB_KIND_NONE: sp_unreachable_case();
      }
      break;
    }
    case SPN_TARGET_KIND_EXE:
    case SPN_TARGET_KIND_TOOL:
    case SPN_TARGET_KIND_TEST:
    case SPN_TARGET_KIND_EXAMPLE: {
      unit->kind = SPN_CC_OUTPUT_EXE;
      break;
    }
    case SPN_TARGET_KIND_CONFIGURE_METAPROGRAM:
    case SPN_TARGET_KIND_BUILD_METAPROGRAM: {
      unit->kind = SPN_CC_OUTPUT_REACTOR;
      break;
    }
  }

  si_da_for_value(target->deps, name) {
    spn_target_unit_t* dep = SP_NULLPTR;
    spn_try(si_add_target_unit(u, pkg, spn_pkg_get_target(&pkg->pkg, name.id, SPN_TARGET_KIND_LIB), &dep));
    si_da_push(u->mem, unit->deps, dep);
  }

  const consumes_t* rule = &consumes[target->info->kind];
  if (rule->self) {
    spn_pkg_unit_t* self = SP_NULLPTR;
    spn_try(si_add_package_unit_closure(u, pkg->build, pkg->resolved, &self));
    si_da_push(u->mem, unit->edges, ((spn_pkg_edge_t) { .pkg = self }));
    si_da_push(u->mem, unit->includes, self);
  }
  sp_da_for_ptr(pkg->resolved->edges, edge) {
    if (!(rule->take & (1u << edge->kind))) {
      continue;
    }
    spn_pkg_unit_t* dep = SP_NULLPTR;
    spn_try(si_add_package_unit_closure(u, pkg->build, sp_ht_getp(u->resolve, edge->id), &dep));
    si_da_push(u->mem, unit->edges, ((spn_pkg_edge_t) { .pkg = dep, .public = edge->public }));
    si_da_push(u->mem, unit->includes, dep);
  }
  si_da_for_value(unit->deps, lib) {
    si_da_for_ptr(lib->edges, edge) {
      push_include(u->mem, unit, edge->pkg);
    }
  }
  si_da_for(unit->includes, it) {
    si_da_for_ptr(unit->includes[it]->deps, edge) {
      if (edge->public) {
        push_include(u->mem, unit, edge->pkg);
      }
    }
  }
  if (rule->published && pkg->publishes.include) {
    si_da_push(u->mem, unit->includes, pkg);
  }

  append(u->mem, &unit->requires, target->requires);
  si_da_for_value(pkg->pkg.requires, require) {
    if (require.kind == SPN_REQUIREMENT_HOST && !rule->host) {
      continue;
    }
    si_da_push(u->mem, unit->requires, require);
  }
  si_da_for_value(unit->includes, dep) {
    if (dep != pkg) {
      append(u->mem, &unit->requires, dep->pkg.publish.requires);
    }
  }

  *result = unit;
  return SPN_OK;
}
