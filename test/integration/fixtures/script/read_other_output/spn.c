#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 A(spn_t* spn) {
  spn_io_write("/work/gen/a.txt", "A");
  return 0;
}

SPN_EXPORT
s32 B(spn_t* spn) {
  FILE* file = fopen("/work/gen/a.txt", "r");
  if (!file) {
    return 1;
  }
  fclose(file);
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* a = spn_add_node(config, "A");
  spn_node_set_fn(a, "A");
  spn_node_add_output(a, SPN_DIR_WORK, "gen/a.txt");

  spn_node_set_fn(spn_add_node(config, "B"), "B");
  return SPN_OK;
}
