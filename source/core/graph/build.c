#include "sp.h"
#include "macro/macro.h"
#include "cc.h"
#include "ctx/types.h"
#include "spn/core.h"
#include "event/types.h"
#include "core/types.h"
#include "unit/types.h"

#include "compiler/driver.h"
#include "dag/dag.h"
#include "enum/enum.h"
#include "event/event.h"
#include "paths/paths.h"
#include "pkg/types.h"
#include "session/invocation.h"
#include "session/session.h"
#include "unit/unit.h"
#include "graph/build.h"
#include "toolchain/linker.h"
#include "profile/types.h"
#include "triple/triple.h"

spn_path_t spn_target_exports_path(sp_mem_t mem, spn_target_unit_t* target) {
  spn_cc_exports_format_t format = spn_cc_exports_format(target->kind, spn_os_to_native_object_format(target->pkg->build->profile.os));

  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  sp_str_t file_name = sp_fmt(s.mem, "{}.{}", sp_fmt_str(target->info->name), sp_fmt_cstr(spn_cc_exports_extension(format))).value;
  spn_path_t path = spn_path_join(mem, target->pkg->paths.work, file_name);
  sp_mem_end_scratch(s);
  return path;
}

spn_path_t spn_target_unit_staged_path(sp_mem_t mem, spn_target_unit_t* target) {
  if (target->kind != SPN_CC_OUTPUT_EXE) return sp_zero_s(spn_path_t);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  sp_str_t file_name = spn_triple_exe_file_name(s.mem, spn_profile_triple(&target->pkg->build->profile), target->info->name);
  spn_path_t root = target->pkg->build->paths.root;

  spn_path_t path = sp_zero;
  switch (target->info->kind) {
    case SPN_TARGET_KIND_EXE:
    case SPN_TARGET_KIND_SCRIPT: {
      path = spn_path_join(mem, root, file_name);
      break;
    }
    case SPN_TARGET_KIND_TEST: {
      path = spn_path_join(mem, spn_path_join(s.mem, root, SP_LIT("test")), file_name);
      break;
    }
    case SPN_TARGET_KIND_EXAMPLE: {
      path = spn_path_join(mem, spn_path_join(s.mem, root, SP_LIT("example")), file_name);
      break;
    }
    case SPN_TARGET_KIND_LIB:
    case SPN_TARGET_KIND_CONFIGURE_METAPROGRAM:
    case SPN_TARGET_KIND_BUILD_METAPROGRAM: {
      break;
    }
  }

  sp_mem_end_scratch(s);
  return path;
}

spn_profile_info_t spn_dag_build_profile(spn_dag_t* g, const spn_dag_build_ctx_t* build) {
  spn_profile_info_t profile = build->unit->profile;
  switch (profile.sdk.kind) {
    case SPN_SDK_NONE:
    case SPN_SDK_SYSROOT:
    case SPN_SDK_MACOS:
    case SPN_SDK_MSVC: {
      break;
    }
    case SPN_SDK_LIBC: {
      profile.sdk.libc.file = spn_dag_find_artifact(g, build->libc)->materialized;
      break;
    }
  }
  return profile;
}
