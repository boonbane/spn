#include "spn.h"

#include <dirent.h>
#include <stdio.h>

SPN_EXPORT
s32 n(spn_t* spn) {
  DIR* dir = opendir("/source");
  if (!dir) {
    return 1;
  }
  while (readdir(dir)) {
  }
  closedir(dir);
  FILE* file = fopen("/work/N.h", "w");
  if (!file) {
    return 1;
  }
  fputs("#define N\n", file);
  fclose(file);
  return 0;
}
