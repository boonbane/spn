#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  FILE* file = fopen("/source/H/a.h", "r");
  if (!file) {
    return 1;
  }
  c8 content [64] = { 0 };
  fread(content, 1, sizeof(content) - 1, file);
  fclose(file);
  spn_io_write(work, "gen/G/a.h", content);
  return 0;
}
