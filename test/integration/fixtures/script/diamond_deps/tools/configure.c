#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_add_include(config, spn_get_dir(spn, SPN_DIR_WORK));

  const c8* a_h = spn_get_subdir(spn, SPN_DIR_WORK, "A.h");
  const c8* l_h = spn_get_subdir(spn, SPN_DIR_WORK, "L.h");
  const c8* r_h = spn_get_subdir(spn, SPN_DIR_WORK, "R.h");

  spn_node_t* a = spn_add_node(config, "A");
  spn_node_set_fn(a, "gen_a");
  spn_node_add_output(a, SPN_DIR_WORK, "A.h");

  spn_node_t* l = spn_add_node(config, "L");
  spn_node_set_fn(l, "gen_l");
  spn_node_add_input(l, a_h);
  spn_node_add_output(l, SPN_DIR_WORK, "L.h");

  spn_node_t* r = spn_add_node(config, "R");
  spn_node_set_fn(r, "gen_r");
  spn_node_add_input(r, a_h);
  spn_node_add_output(r, SPN_DIR_WORK, "R.h");

  spn_node_t* f = spn_add_node(config, "F");
  spn_node_set_fn(f, "gen_f");
  spn_node_add_input(f, l_h);
  spn_node_add_input(f, r_h);
  spn_node_add_output(f, SPN_DIR_WORK, "F.h");
  return SPN_OK;
}
