#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/X.h", "#define X 1\n");
  spn_io_write("/work/Y.h", "#define Y 2\n");
  spn_io_write("/work/Z.h", "#define Z 3\n");
  return 0;
}
