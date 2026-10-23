#include "spn.h"

#include <stdio.h>

static s32 read_int(const c8* path, s32* value) {
  FILE* file = fopen(path, "r");
  if (!file) {
    return 1;
  }
  s32 read = fscanf(file, "%d", value);
  fclose(file);
  return read == 1 ? 0 : 1;
}

static void write_int(const c8* path, s32 value) {
  c8 line [16];
  snprintf(line, sizeof(line), "%d", value);
  spn_io_write(path, line);
}

SPN_EXPORT
s32 gen_a(spn_t* spn) {
  s32 a = 0;
  if (read_int("/source/a.txt", &a)) {
    return 1;
  }
  write_int("/work/A.txt", a);
  return 0;
}

SPN_EXPORT
s32 gen_b(spn_t* spn) {
  s32 a = 0;
  if (read_int("/work/A.txt", &a)) {
    return 1;
  }
  write_int("/work/B.txt", a + 1);
  return 0;
}
