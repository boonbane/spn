#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));

  spn_node_t* setup = spn_add_node(config, "setup");
  spn_node_set_fn(setup, "setup_phase");

  spn_node_t* info = spn_add_node(config, "gen_info");
  spn_node_set_fn(info, "gen_info");
  spn_node_add_output(info, SPN_DIR_WORK, "dep_info.h");
  spn_node_link(setup, info);
  return SPN_OK;
}
