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

static s32 write_value(const c8* path, const c8* name, s32 value) {
  FILE* file = fopen(path, "w");
  if (!file) {
    return 1;
  }
  fprintf(file, "#define %s %d\n", name, value);
  fclose(file);
  return 0;
}

SPN_EXPORT
s32 gen_a(spn_t* spn) {
  return write_value("/work/A.h", "A", 1);
}

SPN_EXPORT
s32 gen_l(spn_t* spn) {
  s32 a = read_value("/work/A.h");
  if (a < 0) {
    return 1;
  }
  return write_value("/work/L.h", "L", a * 2);
}

SPN_EXPORT
s32 gen_r(spn_t* spn) {
  s32 a = read_value("/work/A.h");
  if (a < 0) {
    return 1;
  }
  return write_value("/work/R.h", "R", a * 3);
}

SPN_EXPORT
s32 gen_f(spn_t* spn) {
  s32 l = read_value("/work/L.h");
  s32 r = read_value("/work/R.h");
  if (l < 0 || r < 0) {
    return 1;
  }
  return write_value("/work/F.h", "F", l + r);
}
