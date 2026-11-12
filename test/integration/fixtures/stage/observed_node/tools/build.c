#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/work/G.txt", "w");
  if (!file) {
    return 1;
  }
  fputs("G", file);
  fclose(file);
  return 0;
}

SPN_EXPORT
s32 probe(spn_t* spn) {
  FILE* file = fopen("/source/gen/G.txt", "r");
  if (file) {
    fclose(file);
  }
  return 0;
}
