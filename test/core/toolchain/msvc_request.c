#include "toolchain.h"

typedef struct {
  const c8* name;
  const c8* str;
  bool expect;
} test_t;

static const test_t toolset_tests [] = {
  { "generation", "v143", true },
  { "year", "2022", true },
  { "prefix", "14.29", true },
  { "unknown_generation", "v140" },
  { "unknown_year", "2015" },
  { "vs_major_is_not_a_toolset", "17" },
  { "trailing_dot", "14." },
};

static const test_t sdk_tests [] = {
  { "prefix", "10.0", true },
  { "four_components", "10.0.22621.0", true },
  { "not_numeric", "A" },
  { "generation_name", "v142" },
  { "empty_component", "10..0" },
  { "five_components", "1.2.3.4.5" },
};

sp_test_each(msvc_request, toolset, test_t, toolset_tests) {
  sp_expect_eq(t, it->expect, si_msvc_toolset_valid(sp_cstr_as_str(it->str)));
  return SP_OK;
}

sp_test_each(msvc_request, sdk, test_t, sdk_tests) {
  sp_expect_eq(t, it->expect, si_msvc_sdk_valid(sp_cstr_as_str(it->str)));
  return SP_OK;
}
