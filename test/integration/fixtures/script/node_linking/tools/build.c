#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen_p(spn_t* spn) {
  FILE* file = fopen("/work/P.txt", "w");
  if (!file) {
    return 1;
  }
  fputs("P", file);
  fclose(file);
  return 0;
}

SPN_EXPORT
s32 check_p(spn_t* spn) {
  FILE* file = fopen("/work/P.txt", "r");
  if (!file) {
    return 1;
  }
  fclose(file);
  file = fopen("/work/S.txt", "w");
  if (!file) {
    return 1;
  }
  fputs("S", file);
  fclose(file);
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
  FILE* file = fopen("/work/G.h", "w");
  if (!file) {
    return 1;
  }
  fputs("#define G 1\n", file);
  fclose(file);
  return 0;
}
