#include "spn.h"

#include <stdio.h>
#include <sys/stat.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  FILE* file = fopen("/source/H/a.h", "r");
  if (!file) {
    return 1;
  }
  c8 content [64] = { 0 };
  fread(content, 1, sizeof(content) - 1, file);
  fclose(file);
  mkdir("/work/gen/G", 0755);
  file = fopen("/work/gen/G/a.h", "w");
  if (!file) {
    return 1;
  }
  fputs(content, file);
  fclose(file);
  return 0;
}
