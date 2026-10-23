#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen_p(spn_t* spn) {
  spn_io_write("/work/P.txt", "P");
  return 0;
}

SPN_EXPORT
s32 check_p(spn_t* spn) {
  FILE* file = fopen("/work/P.txt", "r");
  if (!file) {
    return 1;
  }
  fclose(file);
  return 0;
}

// S has no outputs, so the link makes S's stamp the input of G; if the link
// did nothing, G would run alongside P and find no stamp
SPN_EXPORT
s32 gen_g(spn_t* spn) {
  FILE* stamp = fopen("/work/stamp/S", "r");
  if (!stamp) {
    return 1;
  }
  fclose(stamp);
  spn_io_write("/work/G.h", "#define G 1\n");
  return 0;
}
