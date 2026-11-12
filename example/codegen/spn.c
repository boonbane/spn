#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 generate(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_log(spn, "generating fibonacci.c");

  char body [512] = { 0 };
  int offset = snprintf(body, sizeof(body), "int fibonacci [] = {");
  int a = 0;
  int b = 1;
  for (int i = 0; i < 16; i++) {
    offset += snprintf(body + offset, sizeof(body) - offset, "%s %d", i ? "," : "", a);
    int next = a + b;
    a = b;
    b = next;
  }
  snprintf(body + offset, sizeof(body) - offset, " };\n");

  spn_io_write(work, "generated/fibonacci.c", body);
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
