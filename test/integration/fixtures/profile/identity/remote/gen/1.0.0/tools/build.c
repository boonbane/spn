#include "spn.h"

SPN_EXPORT
s32 gen_header(spn_t* spn) {
  spn_profile_t* profile = spn_get_profile(spn);
  const c8* content = "#define GEN_VALUE 1\n";
  if (spn_profile_get_opt(profile) == SPN_OPT_LEVEL_2) {
    content = "#define GEN_VALUE 2\n";
  }
  if (spn_profile_get_sanitizers(profile) & SPN_SANITIZER_ADDRESS) {
    content = "#define GEN_VALUE 3\n";
  }
  spn_io_write("/work/gen/gen_value.h", content);
  return 0;
}
