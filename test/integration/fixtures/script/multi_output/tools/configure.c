#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));

  spn_node_t* node = spn_add_node(config, "G");
  spn_node_set_fn(node, "gen");
  spn_node_add_output(node, "/work/X.h");
  spn_node_add_output(node, "/work/Y.h");
  spn_node_add_output(node, "/work/Z.h");
  return SPN_OK;
}
