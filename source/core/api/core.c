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
#include "unit/types.h"

#include "enum/enum.h"
#include "error/error.h"
#include "event/event.h"
#include "intern/intern.h"
#include "str/str.h"
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

spn_err_t spn_log(spn_t* s, const c8* message) {
  spn_pkg_unit_t* unit = spn_api_unit(s);
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_USER_LOG,
    .pkg = unit->pkg.manifest->name.str,
    .user_log = { .message = sp_str_from_cstr(spn.events->mem, message) },
  });
  return SPN_OK;
}

spn_profile_t* spn_get_profile(spn_t* s) {
  spn_pkg_unit_t* unit = sp_ptr_cast(spn_pkg_unit_t*, s);
  return sp_ptr_cast(spn_profile_t*, &unit->build->profile);
}

spn_err_t spn_profile_get_os(spn_profile_t* profile, spn_os_t* os) {
  *os = ((spn_profile_info_t*)profile)->target.os;
  return SPN_OK;
}

spn_err_t spn_profile_get_mode(spn_profile_t* profile, spn_mode_t* mode) {
  *mode = ((spn_profile_info_t*)profile)->mode;
  return SPN_OK;
}

spn_err_t spn_profile_get_opt(spn_profile_t* profile, spn_opt_level_t* opt) {
  *opt = ((spn_profile_info_t*)profile)->opt;
  return SPN_OK;
}

spn_err_t spn_profile_get_sanitizers(spn_profile_t* profile, spn_sanitizer_set_t* sanitizers) {
  *sanitizers = ((spn_profile_info_t*)profile)->sanitizers;
  return SPN_OK;
}
