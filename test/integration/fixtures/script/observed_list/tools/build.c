#include "spn.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>

static s32 visible(const struct dirent* entry) {
  return entry->d_name[0] != '.';
}

SPN_EXPORT
s32 n(spn_t* spn) {
  struct dirent** entries = 0;
  s32 count = scandir("/source/D", &entries, visible, alphasort);
  if (count < 0) {
    return 1;
  }
  c8 names [8] = { 0 };
  for (s32 it = 0; it < count; it++) {
    names[it] = entries[it]->d_name[0];
    free(entries[it]);
  }
  free(entries);
  FILE* file = fopen("/work/N", "w");
  if (!file) {
    return 1;
  }
  fputs(names, file);
  fclose(file);
  return 0;
}
