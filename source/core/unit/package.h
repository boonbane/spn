#ifndef SPN_UNIT_PACKAGE_H
#define SPN_UNIT_PACKAGE_H

#include "sp.h"
#include "spn/core.h"
#include "unit/types.h"

static inline bool               spn_pkg_unit_publishes_target(spn_pkg_unit_t* unit, const spn_target_info_t* target) { sp_unreachable_return(false); };
static inline spn_err_t          spn_pkg_unit_publish_headers(spn_pkg_unit_t* ctx, spn_path_t root) { sp_unreachable_return(SPN_ERROR); }
spn_user_output_t  spn_pkg_unit_node_stamp(spn_pkg_unit_t* ctx, spn_user_node_t* node);
void               spn_pkg_unit_announce_compile(spn_pkg_unit_t* ctx);

#endif
