#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/X.h", "#define X 1\n");
  spn_io_write("/work/Y.h", "#define Y 2\n");
  spn_io_write("/work/Z.h", "#define Z 3\n");
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));

  spn_node_t* node = spn_add_node(config, "G");
  spn_node_set_fn(node, "gen");
  spn_node_add_output(node, SPN_DIR_WORK, "X.h");
  spn_node_add_output(node, SPN_DIR_WORK, "Y.h");
  spn_node_add_output(node, SPN_DIR_WORK, "Z.h");
  return SPN_OK;
}
