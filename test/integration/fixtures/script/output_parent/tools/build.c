#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 n(spn_t* spn) {
  FILE* file = fopen("/work/A/B/C", "w");
  if (!file) {
    return 1;
  }
  fclose(file);
  return 0;
}
