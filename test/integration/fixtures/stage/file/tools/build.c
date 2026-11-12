#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  char content [64] = { 0 };
  FILE* file = fopen("/source/I.txt", "r");
  if (!file) {
    return 1;
  }
  fread(content, 1, sizeof(content) - 1, file);
  fclose(file);
  spn_io_write(work, "G.txt", content);
  return 0;
}
