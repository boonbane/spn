#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/G.h", "#define G 1\n");
  return 0;
}
