#include "spn.h"

#include <dirent.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_io_write(work, "G.txt", "A");
  return 0;
}

SPN_EXPORT
s32 list(spn_t* spn) {
  DIR* dir = opendir("/source");
  if (dir) {
    while (readdir(dir)) {
    }
    closedir(dir);
  }
  return 0;
}
