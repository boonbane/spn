#ifndef SPN_UNIT_PACKAGE_H
#define SPN_UNIT_PACKAGE_H

#include "sp.h"
#include "spn/core.h"
#include "unit/types.h"

typedef struct {
  spn_target_map_t maps [4];
  u32 count;
  u32 map;
  u32 target;
  u32 index;
  spn_path_t header;
} spn_pkg_unit_header_it_t;

bool                     spn_pkg_unit_header_it_valid(const spn_pkg_unit_header_it_t* it);
void                     spn_pkg_unit_header_it_next(spn_pkg_unit_header_it_t* it);
spn_pkg_unit_header_it_t spn_pkg_unit_header_it_begin(spn_pkg_unit_t* unit);

#define spn_pkg_unit_for_header(unit, it) \
  for (spn_pkg_unit_header_it_t it = spn_pkg_unit_header_it_begin((unit)); spn_pkg_unit_header_it_valid(&(it)); spn_pkg_unit_header_it_next(&(it)))

spn_user_output_t  spn_pkg_unit_node_stamp(spn_pkg_unit_t* ctx, spn_user_node_t* node);
void               spn_pkg_unit_announce_compile(spn_pkg_unit_t* ctx);
spn_err_t          spn_pkg_unit_publish_headers(spn_pkg_unit_t* ctx, spn_path_t root);

#endif
