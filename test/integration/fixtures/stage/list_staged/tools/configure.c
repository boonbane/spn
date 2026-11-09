#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* gen = spn_add_node(config, "gen");
  spn_node_set_fn(gen, "gen");
  spn_node_add_output_dir(gen, "/work/G");

  spn_node_t* list = spn_add_node(config, "list");
  spn_node_set_fn(list, "list");
  return SPN_OK;
}
