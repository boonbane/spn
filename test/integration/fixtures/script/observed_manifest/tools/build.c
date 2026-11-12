#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 n(spn_t* spn) {
  FILE* file = fopen("/manifest/X", "r");
  if (!file) {
    return 1;
  }
  c8 content [2] = { 0 };
  fread(content, 1, 1, file);
  fclose(file);
  spn_io_write("/work/N", content);
  return 0;
}
