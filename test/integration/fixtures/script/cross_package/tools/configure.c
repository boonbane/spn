#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* info = spn_add_node(config, "gen_info");
  spn_node_set_fn(info, "gen_info");
  spn_node_add_output(info, "/work/dep_info.h");
  return SPN_OK;
}
