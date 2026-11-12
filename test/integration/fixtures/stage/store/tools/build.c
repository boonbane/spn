#include "spn.h"

SPN_EXPORT
s32 gen(spn_t* spn) {
  spn_root_t store = 0;
  spn_try(spn_get_root_store(spn, &store));
  spn_io_write(store, "vendor/G.txt", "A");
  return 0;
}
