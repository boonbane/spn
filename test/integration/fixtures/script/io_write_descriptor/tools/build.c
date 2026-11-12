#include "spn.h"

#include <fcntl.h>

SPN_EXPORT
spn_err_t N(spn_t* spn) {
  s32 dir = open("/work", O_RDONLY | O_DIRECTORY);
  if (dir < 0) {
    return SPN_ERROR;
  }
  return spn_io_write(dir, "X", "A") == SPN_ERR_PATH_NOT_MOUNTED ? SPN_OK : SPN_ERROR;
}
