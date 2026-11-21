#include "toolchain.h"

typedef struct {
  const c8* name;
  const c8* toolset;
  const c8* sdk;
} test_t;

static const test_t tests [] = {
  { .name = "unknown_generation", .toolset = "v140" },
  { .name = "unknown_year", .toolset = "2015" },
  { .name = "vs_major_is_not_a_toolset", .toolset = "17" },
  { .name = "trailing_dot", .toolset = "14." },
  { .name = "sdk_is_numeric", .sdk = "A" },
  { .name = "sdk_has_no_generations", .sdk = "v142" },
  { .name = "empty_component", .sdk = "10..0" },
  { .name = "five_components", .sdk = "1.2.3.4.5" },
};

sp_test_each(msvc_request, invalid, test_t, tests) {
  sp_expect(t, !si_msvc_toolset_valid(sp_cstr_as_str(it->toolset)) || !si_msvc_sdk_valid(sp_cstr_as_str(it->sdk)));
  return SP_OK;
}
