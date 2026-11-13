#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/work/V.txt", "w");
  if (!file) {
    return 1;
  }
  fputs("1", file);
  fclose(file);
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_node_t* node = spn_add_node(config, "gen");
  spn_node_set_fn(node, "gen");
  spn_node_add_output(node, work, "V.txt");
  return SPN_OK;
}
