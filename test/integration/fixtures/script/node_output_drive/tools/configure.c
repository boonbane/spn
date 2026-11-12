#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  return spn_node_add_output(spn_add_node(config, "N"), "/store/C:/X") == SPN_ERR_PATH_NOT_NORMAL ? SPN_OK : SPN_ERROR;
}
