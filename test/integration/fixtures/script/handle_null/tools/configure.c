#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  return spn_node_set_fn(0, "N") == SPN_ERR_HANDLE_NULL ? SPN_OK : SPN_ERROR;
}
