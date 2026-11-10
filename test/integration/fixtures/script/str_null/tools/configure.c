#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  return spn_log(spn, 0) == SPN_ERR_STR_NULL ? SPN_OK : SPN_ERROR;
}
