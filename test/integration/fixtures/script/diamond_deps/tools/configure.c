#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_node_t* a = spn_add_node(config, "A");
  spn_node_set_fn(a, "gen_a");
  spn_node_add_output(a, "/work/A.h");

  spn_node_t* l = spn_add_node(config, "L");
  spn_node_set_fn(l, "gen_l");
  spn_node_add_output(l, "/work/L.h");
  spn_node_link(a, l);

  spn_node_t* r = spn_add_node(config, "R");
  spn_node_set_fn(r, "gen_r");
  spn_node_add_output(r, "/work/R.h");
  spn_node_link(a, r);

  spn_node_t* f = spn_add_node(config, "F");
  spn_node_set_fn(f, "gen_f");
  spn_node_add_output(f, "/work/F.h");
  spn_node_link(l, f);
  spn_node_link(r, f);
  return SPN_OK;
}
