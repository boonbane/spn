#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  return spn_fmt("{}") ? SPN_ERROR : SPN_OK;
}
