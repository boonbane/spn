#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "gen");
  spn_node_set_fn(node, "gen");
  spn_node_add_input(node, spn_get_subdir(spn, SPN_DIR_SOURCE, "H/a.h"));
  spn_node_add_output_dir(node, SPN_DIR_WORK, "gen/G");
  spn_target_embed_dir_ex(spn_get_target(spn, "M", SPN_TARGET_KIND_EXE), spn_get_subdir(spn, SPN_DIR_WORK, "gen/G"), "G", "unsigned char", "unsigned long long");
  return SPN_OK;
}
