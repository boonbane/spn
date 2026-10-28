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
#include "pkg/pkg.h"
#include "session/session.h"
#include "target/select.h"
#include "profile/types.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"

spn_err_t spn_target_unit_ensure(spn_session_t* s, spn_pkg_unit_t* pkg, si_target_t* target, spn_target_unit_t** result) {
  spn_target_unit_t* unit = spn_session_find_target_in_pkg(s, pkg, target->info->id);
  if (unit) {
    *result = unit;
    return SPN_OK;
  }

  spn_target_unit_id_t id = {
    .pkg = pkg->id,
    .target = target->info->id,
  };
  sp_om_insert(s->units.targets, id, sp_zero_s(spn_target_unit_t));
  unit = sp_om_back(s->units.targets);
  unit->id = id;
  unit->pkg = pkg;
  unit->target = target;
  sp_da_init(s->mem, unit->objects);
  sp_da_init(s->mem, unit->deps);

  switch (target->info->kind) {
    case SPN_TARGET_KIND_LIB: {
      si_da_push(s->mem, pkg->libs, unit);

      if (spn_linkage_set_has(target->info->linkages, SPN_LIB_KIND_OBJECT) || target->info->no_link) {
        unit->lib_kind = spn_linkage_set_default(target->info->linkages);
      }
      else {
        const spn_profile_info_t* profile = &pkg->build->profile;
        spn_kind_query_t query = {
          .config = spn_session_config_kind(s, pkg->pkg.manifest->name),
          .linkage = profile->linking.linkage,
        };

        if (spn_target_select_lib_kind(target->info->linkages, query, &unit->lib_kind)) {
          spn_linkage_requester_t requester = SPN_LINKAGE_REQUESTER_TARGET;
          if (profile->request.linkage) {
            requester = SPN_LINKAGE_REQUESTER_PROFILE;
          }
          else if (profile->request.libc == SPN_RUNTIME_STATIC) {
            requester = SPN_LINKAGE_REQUESTER_LIBC;
          }
          sp_da(spn_linkage_t) supported = sp_da_new(s->ctx->events->mem, spn_linkage_t);
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
          return spn_err_emit(s->ctx, (spn_err_union_t) {
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
      sp_assert(pkg->build == s->units.metaprogram);
      unit->kind = SPN_CC_OUTPUT_REACTOR;
      break;
    }
  }

  spn_triple_t triple = spn_profile_triple(&pkg->build->profile);
  switch (unit->kind) {
    case SPN_CC_OUTPUT_EXE: {
      unit->file_name = spn_triple_exe_file_name(s->mem, triple, target->info->name.str);
      break;
    }
    case SPN_CC_OUTPUT_STATIC_LIB: {
      unit->file_name = spn_triple_lib_file_name(s->mem, triple, target->info->name.str, SP_OS_LIB_STATIC);
      break;
    }
    case SPN_CC_OUTPUT_SHARED_LIB: {
      unit->file_name = spn_triple_lib_file_name(s->mem, triple, target->info->name.str, SP_OS_LIB_SHARED);
      break;
    }
    case SPN_CC_OUTPUT_REACTOR: {
      unit->file_name = sp_fmt(s->mem, "{}.wasm", sp_fmt_str(target->info->name.str)).value;
      break;
    }
    case SPN_CC_OUTPUT_OBJECT: {
      break;
    }
  }

  *result = unit;
  return SPN_OK;
}
