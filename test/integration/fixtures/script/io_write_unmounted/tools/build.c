#include "spn.h"

SPN_EXPORT
spn_err_t N(spn_t* spn) {
  return spn_io_write("/X", "A") == SPN_ERR_PATH_NOT_MOUNTED ? SPN_OK : SPN_ERROR;
}
