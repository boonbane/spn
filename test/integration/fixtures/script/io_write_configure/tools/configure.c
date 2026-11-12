#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  return spn_io_write("/work/X", "A") == SPN_ERR_PATH_NOT_MOUNTED ? SPN_OK : SPN_ERROR;
}
