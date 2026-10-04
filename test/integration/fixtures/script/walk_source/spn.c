#include "spn.h"

#include <dirent.h>
#include <stdio.h>

static void walk(const c8* path) {
  DIR* dir = opendir(path);
  if (!dir) {
    return;
  }
  struct dirent* entry = NULL;
  while ((entry = readdir(dir))) {
    if (entry->d_type == DT_DIR && entry->d_name[0] != '.') {
      c8 child [1024] = { 0 };
      snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
      walk(child);
    }
  }
  closedir(dir);
}

SPN_EXPORT
s32 gen(spn_t* spn) {
  walk("/source");
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_set_fn(spn_add_node(config, "N"), "gen");
  return SPN_OK;
}
