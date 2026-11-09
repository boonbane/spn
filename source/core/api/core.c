#include "sp.h"
#include "macro/macro.h"
#include "spn.h"

#include "api/api.h"
#include "api/types.h"
#include "core/types.h"
#include "ctx/types.h"
#include "event/types.h"
#include "paths/paths.h"
#include "pkg/types.h"
#include "target/types.h"
#include "unit/types.h"

#include "enum/enum.h"
#include "error/error.h"
#include "event/event.h"
#include "external/wasm/wasm.h"
#include "intern/intern.h"
#include "str/str.h"
#include "pkg/pkg.h"
#include "io/io.h"

spn_pkg_unit_t* spn_api_unit(const void* opaque) {
  return (spn_pkg_unit_t*)opaque;
}

spn_path_t spn_api_dir_path(spn_pkg_unit_t* unit, spn_dir_t dir) {
  switch (dir) {
    case SPN_DIR_NONE:     return sp_zero_struct(spn_path_t);
    // Units have no distinct cache dir; CACHE aliases the store
    case SPN_DIR_CACHE:    return unit->paths.store;
    case SPN_DIR_STORE:    return unit->paths.store;
    case SPN_DIR_INCLUDE:  return unit->paths.include;
    case SPN_DIR_VENDOR:   return unit->paths.vendor;
    case SPN_DIR_LIB:      return unit->paths.lib;
    case SPN_DIR_SOURCE:   return unit->paths.roots.source;
    case SPN_DIR_WORK:     return unit->paths.work;
    case SPN_DIR_PROJECT:  return spn_path_from_root(SPN_PATH_ROOT_PROJECT);
    case SPN_DIR_MANIFEST: return unit->paths.roots.recipe;
    case SPN_DIR_BIN:      return unit->paths.bin;
  }

  SP_UNREACHABLE_RETURN(sp_zero_struct(spn_path_t));
}

static void reject(const c8* fn, sp_str_t path, const c8* reason) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t message = sp_fmt(scratch.mem, "{}: {} {}", SP_FMT_CSTR(fn), SP_FMT_STR(path), SP_FMT_CSTR(reason)).value;
  bool trapped = spn_wasm_trap(message);
  sp_mem_end_scratch(scratch);
  sp_assert(trapped);
}

static spn_pkg_unit_t* find_dep(spn_pkg_unit_t* pkg, sp_intern_id_t name, spn_dep_kind_t kind) {
  sp_da_for_ptr(pkg->resolved->edges, edge) {
    if (edge->kind != kind || edge->id.name != name) {
      continue;
    }
    spn_pkg_unit_id_t id = { .pkg = edge->id, .build = pkg->build->id };
    if (!sp_om_has(pkg->units->packages, id)) {
      return SP_NULLPTR;
    }
    return sp_om_get(pkg->units->packages, id);
  }
  return SP_NULLPTR;
}

static bool foreign(spn_pkg_unit_t* unit, spn_path_t path) {
  if (path.root != SPN_PATH_ROOT_NONE || spn_tree_rel(unit->paths.roots, path).tree != SPN_TREE_NONE) {
    return false;
  }
  si_da_for(unit->pkg.manifest->deps, it) {
    spn_requested_dep_t* dep = &unit->pkg.manifest->deps[it];
    spn_pkg_unit_t* dep_unit = find_dep(unit, dep->name.id, dep->kind);
    if (dep_unit && spn_tree_rel(dep_unit->paths.roots, path).tree != SPN_TREE_NONE) {
      return false;
    }
  }
  return true;
}

spn_path_t spn_api_tree_path(spn_pkg_unit_t* unit, const c8* fn, const c8* path) {
  sp_str_t str = sp_cstr_as_str(path);
  if (!spn_path_normal(str)) {
    reject(fn, str, "must not contain '.', '..', or empty components");
    return sp_zero_struct(spn_path_t);
  }
  if (!sp_fs_is_absolute(str)) {
    return spn_tree_path(spn.mem, &unit->units->ctx->roots, unit->paths.roots, SPN_TREE_SOURCE, str);
  }
  spn_path_t absolute = spn_path_make(&unit->units->ctx->roots, str);
  if (foreign(unit, absolute)) {
    reject(fn, str, "is outside every directory spn manages");
    return sp_zero_struct(spn_path_t);
  }
  return spn_path_copy(spn.mem, absolute);
}

static spn_path_t api_path(spn_pkg_unit_t* unit, const c8* fn, const c8* path) {
  return spn_api_tree_path(unit, fn, path);
}

static spn_target_t* wrap(spn_pkg_unit_t* unit, si_target_t* eval) {
  spn_target_t* target = sp_alloc_type(spn.mem, spn_target_t);
  target->unit = unit;
  target->target = eval;
  return target;
}

spn_target_t* spn_get_target(spn_t* spn, const c8* name, spn_target_kind_t kind) {
  spn_pkg_unit_t* unit = spn_api_unit(spn);
  si_target_t* eval = spn_pkg_get_target(&unit->pkg, spn_intern_cstr(name).id, kind);
  return eval ? wrap(unit, eval) : SP_NULLPTR;
}

bool spn_api_name_rejected(const c8* fn, const c8* name) {
  sp_str_t str = sp_cstr_as_str(name);
  if (!sp_str_contains(str, sp_str_lit("/"))) {
    return false;
  }
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t message = sp_fmt(scratch.mem, "{}: {} may not contain '/'", SP_FMT_CSTR(fn), SP_FMT_STR(str)).value;
  bool trapped = spn_wasm_trap(message);
  sp_assert(trapped);
  sp_mem_end_scratch(scratch);
  return true;
}

void spn_add_system_dep(spn_config_t* config, const c8* dep) {
  spn_pkg_unit_t* unit = spn_api_unit(config);
  sp_mem_t mem = unit->units->mem;
  si_da_push(mem, unit->pkg.system_deps, sp_str_from_cstr(mem, dep));
}

void spn_log(spn_t* s, const c8* message) {
  spn_pkg_unit_t* unit = spn_api_unit(s);
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_USER_LOG,
    .pkg = unit->pkg.manifest->name.str,
    .user_log = { .message = sp_str_from_cstr(spn.events->mem, message) },
  });
}

spn_profile_t* spn_get_profile(spn_t* s) {
  spn_pkg_unit_t* unit = sp_ptr_cast(spn_pkg_unit_t*, s);
  return sp_ptr_cast(spn_profile_t*, &unit->build->profile);
}

spn_libc_kind_t spn_profile_get_libc(spn_profile_t* profile) {
  spn_profile_info_t* info = (spn_profile_info_t*)profile;
  switch (info->target.abi) {
    case SPN_ABI_MUSL: return SPN_LIBC_MUSL;
    case SPN_ABI_BARE: return SPN_LIBC_NONE;
    case SPN_ABI_ELF:  return SPN_LIBC_CUSTOM;
    default:           return SPN_LIBC_GNU;
  }
}

spn_os_t spn_profile_get_os(spn_profile_t* profile) {
  return ((spn_profile_info_t*)profile)->target.os;
}

spn_mode_t spn_profile_get_mode(spn_profile_t* profile) {
  return ((spn_profile_info_t*)profile)->mode;
}

spn_opt_level_t spn_profile_get_opt(spn_profile_t* profile) {
  return ((spn_profile_info_t*)profile)->opt;
}

spn_sanitizer_set_t spn_profile_get_sanitizers(spn_profile_t* profile) {
  return ((spn_profile_info_t*)profile)->sanitizers;
}

void spn_target_add_flag(spn_target_t* target, const c8* flag) {
  sp_mem_t mem = target->unit->units->mem;
  si_da_push(mem, target->target->flags, sp_str_from_cstr(mem, flag));
}
