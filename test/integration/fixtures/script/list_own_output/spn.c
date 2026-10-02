#include "spn.h"

#include <dirent.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/gen/a.txt", "A");
  DIR* dir = opendir("/work/gen");
  if (!dir) {
    return 1;
  }
  while (readdir(dir)) {
  }
  closedir(dir);
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "N");
  spn_node_set_fn(node, "gen");
  spn_node_add_output(node, SPN_DIR_WORK, "gen/a.txt");
  return SPN_OK;
}
