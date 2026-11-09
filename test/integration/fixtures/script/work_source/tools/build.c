#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/G.c", "int g() { return 1; }\n");
  return 0;
}
