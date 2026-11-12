#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* a = spn_add_node(config, "A");
  spn_node_t* b = spn_add_node(config, "B");
  return spn_node_link(a, b) == SPN_ERR_NODE_NO_OUTPUTS ? SPN_OK : SPN_ERROR;
}
