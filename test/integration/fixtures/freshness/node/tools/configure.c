#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));

  spn_node_t* v = spn_add_node(config, "V");
  spn_node_set_fn(v, "gen_version");
  spn_node_add_input(v, spn_get_subdir(spn, SPN_DIR_SOURCE, "version.txt"));
  spn_node_add_output(v, SPN_DIR_WORK, "version.h");
  return SPN_OK;
}
