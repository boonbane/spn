#include "spn.h"

SPN_EXPORT
s32 gen_source(spn_t* spn) {
  spn_io_write("/work/generated/answer.c",
    "int answer(void) {\n"
    "  return 42;\n"
    "}\n"
  );
  return 0;
}
