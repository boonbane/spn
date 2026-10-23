#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_add_output_dir(spn_add_node(config, "T"), SPN_DIR_WORK, "G");
  spn_node_add_output(spn_add_node(config, "F"), SPN_DIR_WORK, "G/X.h");
  return SPN_OK;
}
