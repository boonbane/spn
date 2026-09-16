#include "spn.h"
#include "local.h"
#include "probe.h"

#ifndef LOCAL_VALUE
#error "local.h from [package.build] include was not on the include path"
#endif

#ifndef PROBE_VALUE
#error "probe.h from the build dep was not on the include path"
#endif

SPN_EXPORT s32 gen_version(spn_t* spn) {
  spn_write_file(spn, "version.h", "#define BUILD_SCRIPT_VERSION 69\n");
  return 0;
}
