#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/O.h", "#define O 1\n");
  return 0;
}
