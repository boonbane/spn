#include "spn.h"

SPN_EXPORT
spn_err_t N(spn_t* spn) {
  return spn_io_write("/work/../X", "A") == SPN_ERR_PATH_NOT_NORMAL ? SPN_OK : SPN_ERROR;
}
