#include "spn.h"

#include <string.h>

SPN_EXPORT
spn_err_t configure(spn_t* spn, spn_config_t* config) {
  spn_profile_t* profile = spn_get_profile(spn);
  spn_os_t os = SPN_OS_NONE;
  spn_os_t slots [2] = { SPN_OS_NONE, SPN_OS_NONE };
  c8* misaligned = (c8*)slots + 1;
  spn_try(spn_profile_get_os(profile, &os));
  spn_try(spn_profile_get_os(profile, (spn_os_t*)misaligned));
  return memcmp(misaligned, &os, sizeof(os)) ? SPN_ERROR : SPN_OK;
}
