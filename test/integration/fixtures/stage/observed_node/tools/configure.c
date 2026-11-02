#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* gen = spn_add_node(config, "gen");
  spn_node_set_fn(gen, "gen");
  spn_node_add_output(gen, SPN_DIR_WORK, "G.txt");

  spn_node_t* probe = spn_add_node(config, "probe");
  spn_node_set_fn(probe, "probe");
  return SPN_OK;
}
