#include "unit/unit.h"

#include "ctx/types.h"
#include "enum/enum.h"
#include "error/error.h"
#include "intern/intern.h"
#include "options/options.h"
#include "paths/paths.h"
#include "pkg/id.h"
#include "pkg/pkg.h"
#include "profile/profile.h"
#include "target/mutate.h"
#include "target/select.h"
#include "triple/triple.h"
#include "when/when.h"

spn_pkg_unit_t* si_add_package_unit(spn_units_t* u, spn_build_unit_t* build, const spn_resolved_pkg_t* resolved) {
  spn_pkg_unit_id_t uid = { .pkg = resolved->id, .build = build->id };
  if (sp_om_has(u->packages, uid)) {
    return sp_om_get(u->packages, uid);
  }

  sp_mem_t mem = u->mem;
  spn_when_env_t env = sp_zero;
  spn_profile_env(mem, &build->profile, &env);
  spn_options_env_set(&env, &resolved->options.values);

  sp_om_insert(u->packages, uid, sp_zero_struct(spn_pkg_unit_t));
  spn_pkg_unit_t* unit = sp_om_back(u->packages);
  unit->id = uid;
  unit->build = build;
  unit->units = u;
  unit->resolved = resolved;
  unit->pkg = si_evaluate_package(mem, resolved->manifest, &u->ctx->roots, resolved->roots, &env);
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

  unit->paths.roots = resolved->roots;
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
      spn_pkg_name_t name = spn_pkg_name_from_qualified(resolved->manifest->qualified.str);
      spn_path_t work = spn_path_s(mem, SPN_DIR_ID_BUILD, name.namespace);
      spn_path_t store = spn_path_s(mem, SPN_DIR_ID_STORE, name.namespace);
      unit->paths.work = spn_path_join(mem, spn_path_join(mem, work, name.name), fingerprint);
      unit->paths.store = spn_path_join(mem, spn_path_join(mem, store, name.name), fingerprint);
      break;
    }
  }
  unit->paths.work = spn_path_anchor(mem, &u->ctx->roots, unit->paths.work);
  unit->paths.store = spn_path_anchor(mem, &u->ctx->roots, unit->paths.store);
  unit->paths.include = spn_path_join(mem, unit->paths.store, SP_LIT("include"));
  unit->paths.share = spn_path_join(mem, unit->paths.store, SP_LIT("share"));
  unit->paths.bin = spn_path_join(mem, unit->paths.store, SP_LIT("bin"));
  unit->paths.lib = spn_path_join(mem, unit->paths.store, SP_LIT("lib"));
  unit->paths.vendor = spn_path_join(mem, unit->paths.store, SP_LIT("vendor"));
  return unit;
}

spn_err_t si_add_package_unit_closure(spn_units_t* u, spn_pkg_unit_t* pkg) {
  if (pkg->expanded) {
    return SPN_OK;
  }
  pkg->expanded = true;

  sp_da_for_ptr(pkg->resolved->edges, edge) {
    if (edge->kind != SPN_DEP_KIND_PACKAGE) {
      continue;
    }
    spn_pkg_unit_t* dep = si_add_package_unit(u, pkg->build, sp_ht_getp(u->resolve, edge->id));
    spn_try(si_add_package_unit_closure(u, dep));
    si_da_push(u->mem, pkg->deps, ((spn_pkg_edge_t) { .pkg = dep, .private = edge->private }));
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
      if (spn_linkage_set_has(target->info->linkages, SPN_LIB_KIND_OBJECT) || target->info->no_link) {
        unit->lib_kind = spn_linkage_set_default(target->info->linkages);
      }
      else {
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
          if (target->info->linkages.source) {
            sp_da_push(supported, SPN_LIB_KIND_SOURCE);
          }
          if (target->info->linkages.object) {
            sp_da_push(supported, SPN_LIB_KIND_OBJECT);
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
      }

      switch (unit->lib_kind) {
        case SPN_LIB_KIND_STATIC: unit->kind = SPN_CC_OUTPUT_STATIC_LIB; break;
        case SPN_LIB_KIND_SHARED: unit->kind = SPN_CC_OUTPUT_SHARED_LIB; break;
        case SPN_LIB_KIND_SOURCE:
        case SPN_LIB_KIND_OBJECT: unit->kind = SPN_CC_OUTPUT_OBJECT; break;
        case SPN_LIB_KIND_NONE: break;
      }
      break;
    }
    case SPN_TARGET_KIND_EXE:
    case SPN_TARGET_KIND_SCRIPT:
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

  spn_triple_t triple = spn_profile_triple(&pkg->build->profile);
  switch (unit->kind) {
    case SPN_CC_OUTPUT_EXE: {
      unit->file_name = spn_triple_exe_file_name(u->mem, triple, target->info->name.str);
      break;
    }
    case SPN_CC_OUTPUT_STATIC_LIB: {
      unit->file_name = spn_triple_lib_file_name(u->mem, triple, target->info->name.str, SP_OS_LIB_STATIC);
      break;
    }
    case SPN_CC_OUTPUT_SHARED_LIB: {
      unit->file_name = spn_triple_lib_file_name(u->mem, triple, target->info->name.str, SP_OS_LIB_SHARED);
      break;
    }
    case SPN_CC_OUTPUT_REACTOR: {
      unit->file_name = sp_fmt(u->mem, "{}.wasm", sp_fmt_str(target->info->name.str)).value;
      break;
    }
    case SPN_CC_OUTPUT_OBJECT: {
      break;
    }
  }

  si_da_for_value(target->deps, name) {
    sp_intern_id_t qualified = spn_intern(spn_pkg_canonicalize_name(name)).id;
    bool package = false;
    sp_da_for_ptr(pkg->resolved->edges, edge) {
      if (edge->id.qualified == qualified) {
        package = true;
        break;
      }
    }
    if (package) {
      continue;
    }
    si_target_t* lib = spn_pkg_get_target(&pkg->pkg, name, SPN_TARGET_KIND_LIB);
    if (!lib) {
      return spn_err_emit(u->ctx, (spn_err_union_t) {
        .kind = SPN_ERR_TARGET_DEP,
        .target = { .name = name },
      });
    }
    spn_target_unit_t* dep = SP_NULLPTR;
    spn_try(si_add_target_unit(u, pkg, lib, &dep));
    si_da_push(u->mem, unit->deps, dep);
  }

  u32 take = 0;
  switch (target->info->kind) {
    case SPN_TARGET_KIND_LIB:
    case SPN_TARGET_KIND_EXE:
    case SPN_TARGET_KIND_SCRIPT:
    case SPN_TARGET_KIND_EXAMPLE: {
      take = 1u << SPN_DEP_KIND_PACKAGE;
      break;
    }
    case SPN_TARGET_KIND_TEST: {
      take = (1u << SPN_DEP_KIND_PACKAGE) | (1u << SPN_DEP_KIND_TEST);
      break;
    }
    case SPN_TARGET_KIND_BUILD_METAPROGRAM: {
      take = 1u << SPN_DEP_KIND_BUILD;
      break;
    }
    case SPN_TARGET_KIND_CONFIGURE_METAPROGRAM: {
      break;
    }
  }
  sp_da_for_ptr(pkg->resolved->edges, edge) {
    if (!(take & (1u << edge->kind))) {
      continue;
    }
    spn_pkg_unit_t* dep = si_add_package_unit(u, pkg->build, sp_ht_getp(u->resolve, edge->id));
    spn_try(si_add_package_unit_closure(u, dep));
    si_da_push(u->mem, unit->pkgs, ((spn_pkg_edge_t) { .pkg = dep, .private = edge->private }));
  }

  *result = unit;
  return SPN_OK;
}
