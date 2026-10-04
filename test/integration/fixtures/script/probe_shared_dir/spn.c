#include "spn.h"

#include <stdio.h>
#include <sys/stat.h>

SPN_EXPORT
s32 A(spn_t* spn) {
  struct stat info = { 0 };
  stat("/work/gen", &info);
  mkdir("/work/gen", 0755);
  FILE* file = fopen("/work/gen/a.txt", "w");
  if (!file) {
    return 1;
  }
  fputs("A", file);
  fclose(file);
  return 0;
}

SPN_EXPORT
s32 B(spn_t* spn) {
  spn_io_write("/work/gen/b.txt", "B");
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* a = spn_add_node(config, "A");
  spn_node_set_fn(a, "A");
  spn_node_add_output(a, SPN_DIR_WORK, "gen/a.txt");

  spn_node_t* b = spn_add_node(config, "B");
  spn_node_set_fn(b, "B");
  spn_node_add_output(b, SPN_DIR_WORK, "gen/b.txt");
  return SPN_OK;
}
