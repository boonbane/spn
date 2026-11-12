#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_io_write(work, "G.txt", "G");
  return 0;
}

SPN_EXPORT
s32 probe(spn_t* spn) {
  FILE* file = fopen("/source/gen/G.txt", "r");
  if (file) {
    fclose(file);
  }
  return 0;
}
