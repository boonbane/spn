#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/work/O.h", "w");
  if (!file) {
    return 1;
  }
  fputs("#define O 1\n", file);
  fclose(file);
  return 0;
}
