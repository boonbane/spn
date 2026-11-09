#include "spn.h"

#include <dirent.h>

SPN_EXPORT
s32 n(spn_t* spn) {
  DIR* dir = opendir("/source");
  if (!dir) {
    return 1;
  }
  while (readdir(dir)) {
  }
  closedir(dir);
  spn_io_write("/work/N.h", "#define N\n");
  return 0;
}
