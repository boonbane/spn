#include "spn.h"

#include <stdio.h>

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  FILE* file = fopen("/work/X", "w");
  if (!file) {
    return SPN_ERROR;
  }
  fclose(file);
  return SPN_OK;
}
