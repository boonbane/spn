#ifndef SPN_UNIT_UNIT_H
#define SPN_UNIT_UNIT_H

#include "sp.h"
#include "spn/core.h"
#include "session/types.h"
#include "target/types.h"
#include "unit/types.h"

si_da(spn_closure_entry_t) si_link_get_target_closure(sp_mem_t mem, spn_target_unit_t* root);
si_da(spn_target_unit_t*) si_link_get_target_runtime_libs(sp_mem_t mem, spn_target_unit_t* root);
si_da(spn_link_lib_t) si_link_get_closure_libs(sp_mem_t mem, si_da(spn_closure_entry_t) closure);

spn_build_unit_t* spn_build_add(spn_session_t* session, spn_profile_info_t profile, spn_path_t root, spn_toolchain_info_t* toolchain);
spn_pkg_unit_t*   spn_pkg_unit_ensure(spn_session_t* session, spn_build_unit_t* build, spn_pkg_id_t id);
spn_err_t         spn_target_unit_ensure(spn_session_t* session, spn_pkg_unit_t* pkg, si_target_t* target, spn_target_unit_t** unit);
spn_err_t         spn_closure_new(spn_session_t* session, spn_pkg_unit_t* root, si_da(spn_target_unit_t*) roots, spn_closure_t* closure);
spn_err_t         spn_plan_new(spn_session_t* session, spn_closure_t closure, spn_build_plan_t* plan);

bool spn_dep_kind_applies(spn_dep_kind_t dep, spn_target_kind_t target);

sp_hash_t spn_unit_fingerprint(spn_session_t* session, spn_build_unit_t* build, spn_pkg_id_t id);
sp_str_t  spn_unit_fingerprint_str(sp_mem_t mem, sp_hash_t fingerprint);

#endif
