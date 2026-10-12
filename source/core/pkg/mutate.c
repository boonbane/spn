#include "intern/types.h"
#include "sp.h"
#include "spn/core.h"
#include "index/types.h"
#include "pkg/types.h"
#include "profile/types.h"
#include "toolchain/types.h"

#include "enum/enum.h"
#include "intern/intern.h"
#include "pkg/mutate.h"

void spn_pkg_add_include(sp_mem_t mem, spn_pkg_info_t* pkg, spn_path_t path) {
  si_da_push(mem, pkg->configured.include, path);
}

void spn_pkg_add_define(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t define) {
  si_da_push(mem, pkg->define, sp_str_copy(mem, define));
}

void spn_pkg_add_system_dep(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t dep) {
  si_da_push(mem, pkg->system_deps, sp_str_copy(mem, dep));
}

spn_err_t spn_pkg_add_target(sp_mem_t mem, spn_pkg_info_t* pkg, sp_str_t name, spn_target_kind_t kind, spn_target_info_t** out) {
  sp_intern_str_t interned = spn_intern(name);
  spn_target_id_t id = { .name = interned.id, .kind = kind };
  if (si_om_has(pkg->targets, id)) {
    return SPN_ERR_TARGET_DUPLICATE;
  }

  si_om_insert(mem, pkg->targets, id, ((spn_target_info_t) {
    .id = id,
    .name = interned,
    .kind = kind
  }));
  *out = si_om_back(pkg->targets);
  return SPN_OK;
}
