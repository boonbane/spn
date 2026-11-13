#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen_info(spn_t* spn) {
  FILE* file = fopen("/work/dep_info.h", "w");
  if (!file) {
    return 1;
  }
  fputs(
    "#ifndef DEP_INFO_H\n"
    "#define DEP_INFO_H\n"
    "#define HAS_LOG_DEP 1\n"
    "#endif\n",
    file
  );
  fclose(file);
  return 0;
}
