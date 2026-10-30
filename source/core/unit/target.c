#include "core/types.h"
#include "unit/types.h"
#include "unit/unit.h"

#include "sp.h"
#include "macro/macro.h"
#include "str/str.h"

#include "ctx/types.h"

#include "compiler/driver.h"
#include "error/error.h"
#include "enum/enum.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "target/mutate.h"
#include "pkg/id.h"
#include "pkg/pkg.h"
#include "target/select.h"
#include "profile/types.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

spn_err_t spn_target_unit_ensure(spn_units_t* u, spn_pkg_unit_t* pkg, si_target_t* target, spn_target_unit_t** result) {
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
      si_da_push(u->mem, pkg->libs, unit);

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
    spn_try(spn_target_unit_ensure(u, pkg, lib, &dep));
    si_da_push(u->mem, unit->deps, dep);
  }

  *result = unit;
  return SPN_OK;
}
