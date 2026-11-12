#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* gen = spn_add_node(config, "generate_build_dep_value");
  spn_node_set_fn(gen, "generate_build_dep_value");
  spn_node_add_output(gen, "/work/build_dep_value.h");
  return SPN_OK;
}
