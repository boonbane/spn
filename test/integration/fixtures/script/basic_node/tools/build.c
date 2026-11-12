#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 generate_header(spn_t* spn) {
  spn_log(spn, "generating version.h...");
  FILE* file = fopen("/work/version.h", "w");
  if (!file) {
    return 1;
  }
  fputs(
    "#ifndef VERSION_H\n"
    "#define VERSION_H\n"
    "#define VERSION_MAJOR 69\n"
    "#define VERSION_MINOR 2\n"
    "#define VERSION_PATCH 3\n"
    "#endif\n",
    file
  );
  fclose(file);
  return 0;
}
