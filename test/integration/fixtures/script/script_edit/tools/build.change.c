#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/work/V.txt", "w");
  if (!file) {
    return 1;
  }
  fputs("2", file);
  fclose(file);
  return 0;
}
