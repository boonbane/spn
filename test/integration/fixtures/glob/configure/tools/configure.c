#include "spn.h"

const c8* tag();

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_log(spn, tag());
  return SPN_OK;
}
