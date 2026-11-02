#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_io_write("/work/G.txt", spn_profile_get_mode(spn_get_profile(spn)) == SPN_MODE_RELEASE ? "B" : "A");
  return 0;
}
