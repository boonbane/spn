#include "spn.h"

#include <stdio.h>
#include <sys/stat.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  mkdir("/work/G", 0755);
  FILE* file = fopen("/work/G/H.txt", "w");
  if (!file) {
    return 1;
  }
  fputs("H", file);
  fclose(file);
  return 0;
}
