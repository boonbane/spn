#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_io_write(work, "gen/G/a.h", "#define A 1\n");
  spn_io_write(work, "gen/G/b/c.h", "#define C 3\n");
  return 0;
}
