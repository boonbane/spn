#include "spn.h"

SPN_EXPORT
spn_err_t N(spn_t* spn) {
  return spn_io_write("/work", "A") == SPN_ERR_IO_WRITE ? SPN_OK : SPN_ERROR;
}
