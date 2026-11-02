#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* gen = spn_add_node(config, "gen");
  spn_node_set_fn(gen, "gen");
  spn_node_add_input(gen, spn_get_subdir(spn, SPN_DIR_SOURCE, "I.txt"));
  spn_node_add_output(gen, SPN_DIR_WORK, "G.txt");
  return SPN_OK;
}
