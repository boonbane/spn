#ifndef SPN_UNIT_PACKAGE_H
#define SPN_UNIT_PACKAGE_H

#include "sp.h"
#include "spn/core.h"
#include "unit/types.h"

spn_user_output_t  spn_pkg_unit_node_stamp(spn_pkg_unit_t* ctx, spn_user_node_t* node);
void               spn_pkg_unit_announce_compile(spn_pkg_unit_t* ctx);

#endif
