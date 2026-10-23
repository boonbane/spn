#include "spn.h"

#include <stdio.h>

static s32 read_value(const c8* path) {
  FILE* file = fopen(path, "r");
  if (!file) {
    return -1;
  }
  s32 value = -1;
  if (fscanf(file, "#define %*s %d", &value) != 1) {
    value = -1;
  }
  fclose(file);
  return value;
}

static void write_value(const c8* path, const c8* name, s32 value) {
  c8 line [64];
  snprintf(line, sizeof(line), "#define %s %d\n", name, value);
  spn_io_write(path, line);
}

SPN_EXPORT
s32 gen_a(spn_t* spn) {
  write_value("/work/A.h", "A", 1);
  return 0;
}

SPN_EXPORT
s32 gen_l(spn_t* spn) {
  s32 a = read_value("/work/A.h");
  if (a < 0) {
    return 1;
  }
  write_value("/work/L.h", "L", a * 2);
  return 0;
}

SPN_EXPORT
s32 gen_r(spn_t* spn) {
  s32 a = read_value("/work/A.h");
  if (a < 0) {
    return 1;
  }
  write_value("/work/R.h", "R", a * 3);
  return 0;
}

SPN_EXPORT
s32 gen_f(spn_t* spn) {
  s32 l = read_value("/work/L.h");
  s32 r = read_value("/work/R.h");
  if (l < 0 || r < 0) {
    return 1;
  }
  write_value("/work/F.h", "F", l + r);
  return 0;
}
