#include "spn.h"

#include <dirent.h>

SPN_EXPORT
s32 n(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  DIR* dir = opendir("/source");
  if (!dir) {
    return 1;
  }
  while (readdir(dir)) {
  }
  closedir(dir);
  spn_io_write(work, "N.h", "#define N\n");
  return 0;
}
