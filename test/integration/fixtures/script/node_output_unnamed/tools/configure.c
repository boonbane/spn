#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  return spn_node_add_output_dir(spn_add_node(config, "N"), "/work") == SPN_ERR_OUTPUT_IS_ROOT ? SPN_OK : SPN_ERROR;
}
