#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/source/build/X", "r");
  if (!file) {
    return 1;
  }
  fclose(file);
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_set_fn(spn_add_node(config, "N"), "gen");
  return SPN_OK;
}
