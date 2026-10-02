#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 read(spn_t* spn) {
  FILE* file = fopen("/source/spn.lock", "r");
  if (file) {
    fclose(file);
  }
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* read = spn_add_node(config, "read");
  spn_node_set_fn(read, "read");
  return SPN_OK;
}
