#ifndef SPN_UNIT_UNIT_H
#define SPN_UNIT_UNIT_H

#include "sp.h"
#include "spn/core.h"
#include "target/types.h"
#include "unit/types.h"

spn_build_unit_t* spn_build_add(spn_session_t* session, spn_profile_info_t profile, spn_path_t root, spn_toolchain_info_t* toolchain);
spn_err_t         si_add_package_unit(spn_units_t* units, spn_build_unit_t* build, const spn_resolved_pkg_t* resolved, spn_pkg_unit_t** unit);
spn_err_t         si_add_package_unit_closure(spn_units_t* units, spn_build_unit_t* build, const spn_resolved_pkg_t* resolved, spn_pkg_unit_t** pkg);
spn_err_t         si_add_target_unit(spn_units_t* units, spn_pkg_unit_t* pkg, si_target_t* target, spn_target_unit_t** unit);
spn_err_t         spn_plan_new(spn_units_t* units, si_da(spn_target_unit_t*) roots, spn_build_plan_t* plan);

sp_hash_t spn_unit_fingerprint(spn_units_t* units, spn_build_unit_t* build, const spn_resolved_pkg_t* resolved);
sp_str_t  spn_unit_fingerprint_str(sp_mem_t mem, sp_hash_t fingerprint);

#endif
