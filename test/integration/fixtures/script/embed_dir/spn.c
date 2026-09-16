#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_fs_copy("/source/H/a.h", "/work/gen/G/a.h");
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "gen");
  spn_node_set_fn(node, "gen");
  spn_node_add_input(node, spn_get_subdir(spn, SPN_DIR_SOURCE, "H/a.h"));
  spn_node_add_output_dir(node, SPN_DIR_WORK, "gen/G");
  spn_target_embed_dir_ex(spn_get_target(spn, "M"), spn_get_subdir(spn, SPN_DIR_WORK, "gen/G"), "G", "unsigned char", "unsigned long long");
  return SPN_OK;
}
