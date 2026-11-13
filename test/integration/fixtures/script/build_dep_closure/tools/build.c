#include "spn.h"

#include <stdio.h>

#include "alpha.h"

SPN_EXPORT
s32 gen_closure(spn_t* spn) {
  if (alpha_magic() != 7) {
    spn_log(spn, "unexpected alpha value");
    return 1;
  }

  FILE* file = fopen("/work/closure.h", "w");
  if (!file) {
    return 1;
  }
  fputs(
    "#ifndef CLOSURE_H\n"
    "#define CLOSURE_H\n"
    "#define CLOSURE_VALUE 7\n"
    "#endif\n",
    file
  );
  fclose(file);

  return 0;
}
