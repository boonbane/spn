#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_fs_copy_glob("/source/H/*", "/work/gen/G");
  return 0;
}
