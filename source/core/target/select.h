#ifndef SPN_TARGET_SELECT_H
#define SPN_TARGET_SELECT_H

#include "sp.h"
#include "spn/core.h"

#include "target/types.h"

typedef struct {
  sp_opt_spn_linkage_t config;
  spn_linkage_t linkage;
  bool loader;
} spn_kind_query_t;

spn_linkage_t spn_target_select_lib_kind(spn_linkage_set_t supported, spn_kind_query_t query);

#endif
