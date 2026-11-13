#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_node_add_output_dir(spn_add_node(config, "T"), work, "G");
  spn_node_add_output(spn_add_node(config, "F"), work, "G/X.h");
  return SPN_OK;
}
