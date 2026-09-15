#include "gen_value.h"

#ifndef EXPECT_GEN
#define EXPECT_GEN 1
#endif

#if GEN_VALUE != EXPECT_GEN
#error GEN_VALUE
#endif

int gen_value(void) {
  return GEN_VALUE;
}
