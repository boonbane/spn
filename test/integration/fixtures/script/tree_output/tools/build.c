#include "spn.h"

#include <stdio.h>
#include <sys/stat.h>

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
  mkdir("/work/gen/G", 0755);
  mkdir("/work/gen/G/b", 0755);
  if (write_file("/work/gen/G/a.h", "#define A 1\n")) {
    return 1;
  }
  return write_file("/work/gen/G/b/c.h", "#define C 3\n");
}
