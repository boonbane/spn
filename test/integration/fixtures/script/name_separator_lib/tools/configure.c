#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_add_lib(config, "A/B", SPN_LIB_KIND_STATIC);
  return SPN_OK;
}
