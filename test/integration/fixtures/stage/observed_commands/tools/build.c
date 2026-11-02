#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 probe(spn_t* spn) {
  FILE* file = fopen("/source/compile_commands.json", "r");
  if (file) {
    fclose(file);
  }
  return 0;
}
