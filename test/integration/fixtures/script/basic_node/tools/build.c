#include "spn.h"

SPN_EXPORT
s32 generate_header(spn_t* spn) {
  spn_log(spn, "generating version.h...");
  spn_io_write("/work/version.h",
    "#ifndef VERSION_H\n"
    "#define VERSION_H\n"
    "#define VERSION_MAJOR 69\n"
    "#define VERSION_MINOR 2\n"
    "#define VERSION_PATCH 3\n"
    "#endif\n"
  );
  return 0;
}
