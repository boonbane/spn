#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 generate(spn_t* spn) {
  spn_log(spn, "generating fibonacci.c");

  FILE* file = fopen("/work/generated/fibonacci.c", "w");
  if (!file) {
    return 1;
  }
  fprintf(file, "int fibonacci [] = {");
  int a = 0;
  int b = 1;
  for (int i = 0; i < 16; i++) {
    fprintf(file, "%s %d", i ? "," : "", a);
    int next = a + b;
    a = b;
    b = next;
  }
  fprintf(file, " };\n");
  fclose(file);
  return 0;
}

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_node_t* node = spn_add_node(config, "generate");
  spn_node_set_fn(node, "generate");
  spn_node_add_output(node, work, "generated/fibonacci.c");
  return SPN_OK;
}
