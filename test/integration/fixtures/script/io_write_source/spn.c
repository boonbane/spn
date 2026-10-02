#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_io_write("/source/X", "X");
  return SPN_OK;
}
