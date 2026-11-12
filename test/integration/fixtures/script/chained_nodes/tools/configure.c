#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_node_t* a = spn_add_node(config, "A");
  spn_node_set_fn(a, "gen_a");
  spn_node_add_output(a, work, "A.txt");

  spn_node_t* b = spn_add_node(config, "B");
  spn_node_set_fn(b, "gen_b");
  spn_node_add_output(b, work, "B.txt");
  spn_node_link(a, b);
  return SPN_OK;
}
