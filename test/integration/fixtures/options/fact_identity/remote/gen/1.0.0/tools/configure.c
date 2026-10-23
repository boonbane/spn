#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "gen_header");
  spn_node_set_fn(node, "gen_header");
  spn_node_add_output(node, SPN_DIR_WORK, "gen/gen_value.h");
  spn_target_add_include(spn_get_target(spn, "gen", SPN_TARGET_KIND_LIB), spn_get_subdir(spn, SPN_DIR_WORK, "gen"));
  return SPN_OK;
}
