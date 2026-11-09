#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "gen_header");
  spn_node_set_fn(node, "gen_header");
  spn_node_add_output(node, "/work/gen/gen_value.h");
  return SPN_OK;
}
