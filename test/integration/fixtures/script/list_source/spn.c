#include "spn.h"

#include <dirent.h>

SPN_EXPORT
s32 A(spn_t* spn) {
  DIR* dir = opendir("/source");
  if (!dir) {
    return 1;
  }
  while (readdir(dir)) {
  }
  closedir(dir);
  spn_io_write("/work/A", "A");
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* node = spn_add_node(config, "A");
  spn_node_set_fn(node, "A");
  spn_node_add_output(node, SPN_DIR_WORK, "A");
  return SPN_OK;
}
