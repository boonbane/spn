#include "spn.h"

SPN_EXPORT
s32 gen_header(spn_t* spn) {
  spn_root_t work = 0;
  spn_try(spn_get_root_work(spn, &work));
  spn_mode_t mode = SPN_MODE_NONE;
  spn_try(spn_profile_get_mode(spn_get_profile(spn), &mode));
  const c8* content = mode == SPN_MODE_DEBUG
    ? "#ifdef NDEBUG\n#error \"expected debug profile\"\n#endif\n#define GEN_VALUE 1\n"
    : "#ifndef NDEBUG\n#error \"expected release profile\"\n#endif\n#define GEN_VALUE 2\n";
  spn_io_write(work, "gen/gen_value.h", content);
  return 0;
}
