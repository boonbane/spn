#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_mode_t mode = SPN_MODE_NONE;
  spn_try(spn_profile_get_mode(spn_get_profile(spn), &mode));
  spn_io_write(work, "G.txt", mode == SPN_MODE_RELEASE ? "B" : "A");
  return 0;
}
