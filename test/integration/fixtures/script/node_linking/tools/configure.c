#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));

  spn_node_t* p = spn_add_node(config, "P");
  spn_node_set_fn(p, "gen_p");
  spn_node_add_output(p, SPN_DIR_WORK, "P.txt");

  spn_node_t* s = spn_add_node(config, "S");
  spn_node_set_fn(s, "check_p");
  spn_node_add_input(s, spn_get_subdir(spn, SPN_DIR_WORK, "P.txt"));

  spn_node_t* g = spn_add_node(config, "G");
  spn_node_set_fn(g, "gen_g");
  spn_node_add_output(g, SPN_DIR_WORK, "G.h");
  spn_node_link(s, g);
  return SPN_OK;
}
