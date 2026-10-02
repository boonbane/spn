#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_fs_copy("/source/I.h", "/work/G.h");
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "gen");
  spn_node_set_fn(node, "gen");
  spn_node_add_input(node, spn_get_subdir(spn, SPN_DIR_SOURCE, "I.h"));
  spn_node_add_output(node, SPN_DIR_WORK, "G.h");
  return SPN_OK;
}
