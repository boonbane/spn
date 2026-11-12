#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t store = 0;
  spn_try(spn_get_root_store(spn, &store));
  spn_node_t* node = spn_add_node(config, "gen");
  spn_node_set_fn(node, "gen");
  spn_node_add_output(node, store, "bin/R.txt");
  return SPN_OK;
}
