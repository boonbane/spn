#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/work/gen.h", "w");
  if (!file) {
    return 1;
  }
  fputs("#define G 0\n", file);
  fclose(file);
  return 0;
}
