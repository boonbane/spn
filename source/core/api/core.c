#include "sp.h"
#include "macro/macro.h"
#include "spn.h"

#include "api/api.h"
#include "api/types.h"
#include "core/types.h"
#include "ctx/types.h"
#include "event/types.h"
#include "pkg/types.h"
#include "unit/types.h"

#include "enum/enum.h"
#include "error/error.h"
#include "event/event.h"
#include "external/wasm/wasm.h"
#include "intern/intern.h"
#include "str/str.h"
#include "io/io.h"

spn_pkg_unit_t* spn_api_unit(const void* opaque) {
  return (spn_pkg_unit_t*)opaque;
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

spn_err_t spn_get_root_manifest(spn_t* s, spn_root_t* root) {
  *root = spn_wasm_root(SPN_WASM_MOUNT_ID_MANIFEST);
  return SPN_OK;
}

spn_err_t spn_get_root_source(spn_t* s, spn_root_t* root) {
  *root = spn_wasm_root(SPN_WASM_MOUNT_ID_SOURCE);
  return SPN_OK;
}

spn_err_t spn_get_root_work(spn_t* s, spn_root_t* root) {
  *root = spn_wasm_root(SPN_WASM_MOUNT_ID_WORK);
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
