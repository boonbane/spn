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

spn_path_t spn_target_unit_staged_path(sp_mem_t mem, spn_target_unit_t* target) {
  if (target->kind != SPN_CC_OUTPUT_EXE) return sp_zero_s(spn_path_t);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch_for(mem);
  sp_str_t file_name = spn_triple_exe_file_name(s.mem, spn_profile_triple(&target->pkg->build->profile), target->info->name.str);
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

spn_path_t si_dag_build_libc(spn_dag_t* g, const spn_dag_build_ctx_t* build) {
  spn_path_t libc = sp_zero;
  switch (build->unit->profile.sdk.kind) {
    case SPN_SDK_NONE:
    case SPN_SDK_SYSROOT:
    case SPN_SDK_MACOS:
    case SPN_SDK_MSVC: {
      break;
    }
    case SPN_SDK_LIBC: {
      libc = spn_dag_find_artifact(g, build->libc)->materialized;
      break;
    }
  }
  return libc;
}

sp_da(spn_arg_t) si_dag_build_object_args(sp_mem_t mem, spn_dag_t* g, sp_da(spn_dag_id_t) ids) {
  sp_da(spn_arg_t) args = sp_da_new(mem, spn_arg_t);
  sp_da_for(ids, it) {
    sp_da_push(args, spn_arg_path(spn_dag_find_artifact(g, ids[it])->materialized));
  }
  return args;
}


sp_da(spn_arg_t) si_dag_build_objects(sp_mem_t mem, spn_dag_t* g, const spn_dag_build_objects_t* objects) {
  switch (objects->kind) {
    case SPN_DAG_BUILD_OBJECTS_DIRECT: {
      return si_dag_build_object_args(mem, g, objects->ids);
    }
    case SPN_DAG_BUILD_OBJECTS_RSP: {
      sp_da(spn_arg_t) args = sp_da_new(mem, spn_arg_t);
      sp_da_push(args, spn_arg_glue(sp_str_lit("@"), spn_dag_find_artifact(g, objects->rsp)->materialized));
      return args;
    }
  }
  sp_unreachable_return(SP_NULLPTR);
}

si_da(spn_path_t) si_dag_build_paths(sp_mem_t mem, spn_dag_t* g, si_da(spn_dag_id_t) ids) {
  si_da(spn_path_t) paths = sp_zero;
  si_da_for(ids, it) {
    si_da_push(mem, paths, spn_dag_find_artifact(g, ids[it])->materialized);
  }
  return paths;
}
