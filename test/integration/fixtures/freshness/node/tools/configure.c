#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_node_t* v = spn_add_node(config, "V");
  spn_node_set_fn(v, "gen_version");
  spn_node_add_output(v, work, "version.h");
  return SPN_OK;
}
