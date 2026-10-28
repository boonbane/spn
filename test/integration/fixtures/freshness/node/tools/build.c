#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen_version(spn_t* spn) {
  FILE* file = fopen("/source/version.txt", "r");
  if (!file) {
    return 1;
  }
  s32 version = 0;
  s32 read = fscanf(file, "%d", &version);
  fclose(file);
  if (read != 1) {
    return 1;
  }
  c8 header [64];
  snprintf(header, sizeof(header), "#define VERSION %d\n", version);
  spn_io_write("/work/version.h", header);
  return 0;
}
