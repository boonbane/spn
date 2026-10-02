#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/source/A/B/X", "r");
  if (file) {
    fclose(file);
  }
  spn_io_write("/work/G.txt", "G");
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "gen");
  spn_node_set_fn(node, "gen");
  spn_node_add_output(node, SPN_DIR_WORK, "G.txt");
  return SPN_OK;
}
