#include "spn.h"

#define ROOTS 4

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_root_t roots [ROOTS] = { 0 };
  spn_try(spn_get_root_manifest(spn, &roots[0]));
  spn_try(spn_get_root_source(spn, &roots[1]));
  spn_try(spn_get_root_work(spn, &roots[2]));
  spn_try(spn_get_root_store(spn, &roots[3]));
  for (u32 it = 0; it < ROOTS; it++) {
    for (u32 other = it + 1; other < ROOTS; other++) {
      if (roots[it] == roots[other]) {
        return SPN_ERROR;
      }
    }
  }
  return SPN_OK;
}
