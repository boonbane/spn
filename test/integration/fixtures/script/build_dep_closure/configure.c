#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));

  spn_node_t* gen = spn_add_node(config, "gen_closure");
  spn_node_set_fn(gen, "gen_closure");
  spn_node_add_output(gen, SPN_DIR_WORK, "closure.h");
  return SPN_OK;
}
