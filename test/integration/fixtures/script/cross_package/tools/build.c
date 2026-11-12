#include "spn.h"

SPN_EXPORT
s32 gen_info(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_io_write(work, "dep_info.h",
    "#ifndef DEP_INFO_H\n"
    "#define DEP_INFO_H\n"
    "#define HAS_LOG_DEP 1\n"
    "#endif\n"
  );
  return 0;
}
