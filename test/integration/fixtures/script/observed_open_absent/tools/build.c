#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 n(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  FILE* file = fopen("/source/X", "r");
  if (!file) {
    spn_io_write(work, "N", "A");
    return 0;
  }
  c8 content [2] = { 0 };
  fread(content, 1, 1, file);
  fclose(file);
  spn_io_write(work, "N", content);
  return 0;
}
