#include "spn.h"

#include <stdio.h>

static s32 write_file(const c8* path, const c8* content) {
  FILE* file = fopen(path, "w");
  if (!file) {
    return 1;
  }
  fputs(content, file);
  fclose(file);
  return 0;
}

SPN_EXPORT
s32 gen(spn_t* spn) {
  if (write_file("/work/X.h", "#define X 1\n")) {
    return 1;
  }
  if (write_file("/work/Y.h", "#define Y 2\n")) {
    return 1;
  }
  return write_file("/work/Z.h", "#define Z 3\n");
}
