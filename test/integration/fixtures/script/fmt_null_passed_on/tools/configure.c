#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  return spn_io_write(work, "X", spn_fmt("{}")) == SPN_ERR_STR_NULL ? SPN_OK : SPN_ERROR;
}
