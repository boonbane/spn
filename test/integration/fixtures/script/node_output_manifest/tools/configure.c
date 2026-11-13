#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t manifest = 0;
  spn_try(spn_get_root_manifest(spn, &manifest));
  return spn_node_add_output(spn_add_node(config, "N"), manifest, "X.c") == SPN_ERR_OUTPUT_OUTSIDE ? SPN_OK : SPN_ERROR;
}
