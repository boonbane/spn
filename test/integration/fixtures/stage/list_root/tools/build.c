#include "spn.h"

#include <dirent.h>
#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/work/G.txt", "w");
  if (!file) {
    return 1;
  }
  fputs("A", file);
  fclose(file);
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
