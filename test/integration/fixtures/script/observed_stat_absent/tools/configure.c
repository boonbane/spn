#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "N");
  spn_node_set_fn(node, "n");
  spn_node_add_output(node, SPN_DIR_WORK, "N");
  return SPN_OK;
}
