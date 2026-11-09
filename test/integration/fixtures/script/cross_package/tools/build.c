#include "spn.h"

SPN_EXPORT
s32 gen_info(spn_t* spn) {
  spn_io_write("/work/dep_info.h",
    "#ifndef DEP_INFO_H\n"
    "#define DEP_INFO_H\n"
    "#define HAS_LOG_DEP 1\n"
    "#endif\n"
  );
  return 0;
}
