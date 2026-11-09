#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_add_output(spn_add_node(config, "N"), "/work/../source/X.c");
  return SPN_OK;
}
