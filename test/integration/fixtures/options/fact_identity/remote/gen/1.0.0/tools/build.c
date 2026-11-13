#include "spn.h"

#include <stdio.h>

SPN_EXPORT
s32 gen_header(spn_t* spn) {
  spn_mode_t mode = SPN_MODE_NONE;
  spn_try(spn_profile_get_mode(spn_get_profile(spn), &mode));
  const c8* content = mode == SPN_MODE_DEBUG
    ? "#ifdef NDEBUG\n#error \"expected debug profile\"\n#endif\n#define GEN_VALUE 1\n"
    : "#ifndef NDEBUG\n#error \"expected release profile\"\n#endif\n#define GEN_VALUE 2\n";
  FILE* file = fopen("/work/gen/gen_value.h", "w");
  if (!file) {
    return 1;
  }
  fputs(content, file);
  fclose(file);
  return 0;
}
