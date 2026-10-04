#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/G.txt", "G");
  return 0;
}

SPN_EXPORT
s32 read(spn_t* spn) {
  FILE* file = fopen("/source/gen/G.txt", "r");
  if (file) {
    fclose(file);
  }
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* gen = spn_add_node(config, "gen");
  spn_node_set_fn(gen, "gen");
  spn_node_add_output(gen, SPN_DIR_WORK, "G.txt");

  spn_node_t* read = spn_add_node(config, "read");
  spn_node_set_fn(read, "read");
  return SPN_OK;
}
