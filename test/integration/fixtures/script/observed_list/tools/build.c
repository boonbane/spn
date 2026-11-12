#include "spn.h"

#include <dirent.h>
#include <stdlib.h>

static s32 visible(const struct dirent* entry) {
  return entry->d_name[0] != '.';
}

SPN_EXPORT
s32 n(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
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
  spn_io_write(work, "N", names);
  return 0;
}
