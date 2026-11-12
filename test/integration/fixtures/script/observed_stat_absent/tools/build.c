#include "spn.h"

#include <sys/stat.h>

SPN_EXPORT
s32 n(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  struct stat st = { 0 };
  spn_io_write(work, "N", stat("/source/X", &st) ? "A" : "B");
  return 0;
}
