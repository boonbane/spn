#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_target_add_include(spn_get_target(spn, "main"), spn_get_dir(spn_get_dep(spn, "kit"), SPN_DIR_SHARE));
  return SPN_OK;
}
