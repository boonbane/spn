#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  return spn_node_set_fn(spn_add_node(config, "N"), "N") == SPN_ERR_NODE_NO_SCRIPT ? SPN_OK : SPN_ERROR;
}
