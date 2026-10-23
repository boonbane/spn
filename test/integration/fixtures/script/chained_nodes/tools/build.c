#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen_a(spn_t* spn) {
  spn_fs_copy("/source/a.txt", "/work/A.txt");
  return 0;
}

SPN_EXPORT
s32 gen_b(spn_t* spn) {
  FILE* file = fopen("/work/A.txt", "r");
  if (!file) {
    return 1;
  }
  s32 a = 0;
  s32 read = fscanf(file, "%d", &a);
  fclose(file);
  if (read != 1) {
    return 1;
  }
  c8 line [16];
  snprintf(line, sizeof(line), "%d", a + 1);
  spn_io_write("/work/B.txt", line);
  return 0;
}
