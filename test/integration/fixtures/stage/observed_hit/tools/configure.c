#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_node_t* gen = spn_add_node(config, "gen");
  spn_node_set_fn(gen, "gen");
  spn_node_add_output(gen, work, "G.h");
  return SPN_OK;
}
