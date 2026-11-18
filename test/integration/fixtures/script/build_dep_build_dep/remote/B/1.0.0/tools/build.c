#include "spn.h"

#include <stdio.h>

#include "C.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/work/I/G.h", "w");
  if (!file) {
    return 1;
  }
  fprintf(file, "#define G %d\n", C);
  fclose(file);
  return 0;
}
