#include "spn.h"

#include <stdio.h>

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  return fopen("/source/spn.toml", "r") ? SPN_ERROR : SPN_OK;
}
