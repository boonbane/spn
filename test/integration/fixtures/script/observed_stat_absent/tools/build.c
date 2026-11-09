#include "spn.h"

#include <sys/stat.h>

SPN_EXPORT
s32 n(spn_t* spn) {
  struct stat st = { 0 };
  spn_io_write("/work/N", stat("/source/X", &st) ? "A" : "B");
  return 0;
}
