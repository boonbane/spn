#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  char content [64] = { 0 };
  FILE* file = fopen("/source/I.txt", "r");
  if (!file) {
    return 1;
  }
  fread(content, 1, sizeof(content) - 1, file);
  fclose(file);
  file = fopen("/work/G.txt", "w");
  if (!file) {
    return 1;
  }
  fputs(content, file);
  fclose(file);
  return 0;
}
