#include "spn.h"

#include <sys/stat.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  struct stat info;
  stat("/source/compile_commands.json", &info);
  stat("/source/S", &info);
  spn_io_write("/work/G", "G");
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "gen");
  spn_node_set_fn(node, "gen");
  spn_node_add_output(node, SPN_DIR_WORK, "G");
  return SPN_OK;
}
