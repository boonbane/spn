#include "spn.h"

SPN_EXPORT
s32 gen_info(spn_t* spn) {
  const spn_t* dep = spn_get_dep(spn, "spum");

  if (dep) {
    const c8* dep_include = spn_get_dir(dep, SPN_DIR_INCLUDE);
    spn_log(spn, "found spum include dir");
    (void)dep_include;

    spn_io_write("/work/dep_info.h",
      "#ifndef DEP_INFO_H\n"
      "#define DEP_INFO_H\n"
      "#define HAS_LOG_DEP 1\n"
      "#endif\n"
    );
  }
  else {
    spn_log(spn, "WARNING: spum dependency not found");
    spn_io_write("/work/dep_info.h",
      "#ifndef DEP_INFO_H\n"
      "#define DEP_INFO_H\n"
      "#define HAS_LOG_DEP 0\n"
      "#endif\n"
    );
  }

  return 0;
}

SPN_EXPORT
s32 setup_phase(spn_t* spn) {
  spn_log(spn, "setup_phase: running setup...");
  return 0;
}
