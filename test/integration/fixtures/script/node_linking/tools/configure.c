#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_node_t* p = spn_add_node(config, "P");
  spn_node_set_fn(p, "gen_p");
  spn_node_add_output(p, work, "P.txt");

  spn_node_t* s = spn_add_node(config, "S");
  spn_node_set_fn(s, "check_p");
  spn_node_add_output(s, work, "S.txt");
  spn_node_link(p, s);

  spn_node_t* g = spn_add_node(config, "G");
  spn_node_set_fn(g, "gen_g");
  spn_node_add_output(g, work, "G.h");
  spn_node_link(s, g);
  return SPN_OK;
}
