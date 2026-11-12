#include "spn.h"

SPN_EXPORT
spn_err_t N(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  return spn_io_write(work, "/work/X", "A") == SPN_ERR_PATH_NOT_NORMAL ? SPN_OK : SPN_ERROR;
}
