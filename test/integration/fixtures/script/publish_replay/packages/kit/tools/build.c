#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/gen.h", "#define G 0\n");
  return 0;
}
