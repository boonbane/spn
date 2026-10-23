#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_log(spn, "gen: writing gen.h");
  spn_io_write("/work/gen.h",
    "#ifndef GEN_H\n"
    "#define GEN_H\n"
    "#define GEN 1\n"
    "#endif\n"
  );
  return 0;
}
