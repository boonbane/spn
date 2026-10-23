#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/store/bin/R.txt", "R");
  return 0;
}
