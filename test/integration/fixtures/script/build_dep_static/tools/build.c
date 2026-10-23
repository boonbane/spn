#include "spn.h"

#include "spum.h"

SPN_EXPORT
s32 generate_build_dep_static_value(spn_t* spn) {
  s32 value = spum_magic() + 1;
  if (value != 78) {
    spn_log(spn, "unexpected spum value");
    return 1;
  }

  spn_io_write("/work/build_dep_static_value.h",
    "#ifndef BUILD_DEP_STATIC_VALUE_H\n"
    "#define BUILD_DEP_STATIC_VALUE_H\n"
    "#define BUILD_DEP_STATIC_VALUE 78\n"
    "#endif\n"
  );

  return 0;
}
