#include "spn.h"

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_target_t* main = spn_get_target(spn, "main", SPN_TARGET_KIND_EXE);
  spn_target_embed_file(main, "hello.txt");
  return SPN_OK;
}
