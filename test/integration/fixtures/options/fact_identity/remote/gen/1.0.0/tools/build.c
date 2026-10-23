#include "spn.h"

SPN_EXPORT
s32 gen_header(spn_t* spn) {
  spn_profile_t* profile = spn_get_profile(spn);
  const c8* content = spn_profile_get_mode(profile) == SPN_MODE_DEBUG
    ? "#ifdef NDEBUG\n#error \"expected debug profile\"\n#endif\n#define GEN_VALUE 1\n"
    : "#ifndef NDEBUG\n#error \"expected release profile\"\n#endif\n#define GEN_VALUE 2\n";
  spn_io_write("/work/gen/gen_value.h", content);
  return 0;
}
