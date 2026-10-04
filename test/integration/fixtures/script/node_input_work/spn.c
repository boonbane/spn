#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_add_input(spn_add_node(config, "A"), spn_get_subdir(spn, SPN_DIR_WORK, "A"));
  return SPN_OK;
}
