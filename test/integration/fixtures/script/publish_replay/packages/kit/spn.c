#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/gen.h", "#define G 0\n");
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "gen");
  spn_node_set_fn(node, "gen");
  spn_node_add_output(node, SPN_DIR_WORK, "gen.h");
  spn_target_add_include(spn_get_target(spn, "kit"), spn_get_dir(spn, SPN_DIR_WORK));
  return SPN_OK;
}
