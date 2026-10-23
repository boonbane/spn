#include "spn.h"

#include "alpha.h"

SPN_EXPORT
s32 gen_closure(spn_t* spn) {
  if (alpha_magic() != 7) {
    spn_log(spn, "unexpected alpha value");
    return 1;
  }

  spn_io_write("/work/closure.h",
    "#ifndef CLOSURE_H\n"
    "#define CLOSURE_H\n"
    "#define CLOSURE_VALUE 7\n"
    "#endif\n"
  );

  return 0;
}
