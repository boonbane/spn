#include "spn.h"

#include <stdio.h>
#include <sys/stat.h>

SPN_EXPORT
s32 n(spn_t* spn) {
  struct stat st = { 0 };
  const c8* content = stat("/source/X", &st) ? "A" : "B";
  FILE* file = fopen("/work/N", "w");
  if (!file) {
    return 1;
  }
  fputs(content, file);
  fclose(file);
  return 0;
}
