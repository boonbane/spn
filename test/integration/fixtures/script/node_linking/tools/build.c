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
  spn_io_write("/work/S.txt", "S");
  return 0;
}

// The link makes S's output the input of G; if the link did nothing, G would
// run alongside P and find no S.txt
SPN_EXPORT
s32 gen_g(spn_t* spn) {
  FILE* witness = fopen("/work/S.txt", "r");
  if (!witness) {
    return 1;
  }
  fclose(witness);
  spn_io_write("/work/G.h", "#define G 1\n");
  return 0;
}
