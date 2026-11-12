#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/work/G.c", "w");
  if (!file) {
    return 1;
  }
  fputs("int g;", file);
  fclose(file);
  return 0;
}
