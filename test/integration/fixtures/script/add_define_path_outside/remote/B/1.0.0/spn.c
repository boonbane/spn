#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_target_add_define_path(spn_get_target(spn, "B"), "G", SPN_DIR_WORK, "gen");
  return SPN_OK;
}
