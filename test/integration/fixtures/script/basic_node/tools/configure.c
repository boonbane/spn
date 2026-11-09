#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* gen = spn_add_node(config, "gen_version");
  spn_node_set_fn(gen, "generate_header");

  spn_node_add_output(gen, "/work/version.h");
  return SPN_OK;
}
