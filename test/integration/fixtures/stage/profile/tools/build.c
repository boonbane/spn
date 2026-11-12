#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_mode_t mode = SPN_MODE_NONE;
  spn_try(spn_profile_get_mode(spn_get_profile(spn), &mode));
  FILE* file = fopen("/work/G.txt", "w");
  if (!file) {
    return 1;
  }
  fputs(mode == SPN_MODE_RELEASE ? "B" : "A", file);
  fclose(file);
  return 0;
}
