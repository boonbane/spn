#include "toolchain.h"

typedef struct {
  bool invalid;
} expect_t;

typedef struct {
  const c8* name;
  const c8* toolset;
  const c8* sdk;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "empty_is_unpinned" },
  { .name = "generation", .toolset = "v142" },
  { .name = "year", .toolset = "2019" },
  { .name = "version_prefix", .toolset = "14.29" },
  { .name = "full_version", .toolset = "14.29.30133" },
  { .name = "unknown_generation", .toolset = "v140", .expect.invalid = true },
  { .name = "unknown_year", .toolset = "2015", .expect.invalid = true },
  { .name = "vs_major_is_not_a_toolset", .toolset = "17", .expect.invalid = true },
  { .name = "trailing_dot", .toolset = "14.", .expect.invalid = true },
  { .name = "sdk_prefix", .sdk = "10.0.22621" },
  { .name = "sdk_is_numeric", .sdk = "A", .expect.invalid = true },
  { .name = "sdk_has_no_generations", .sdk = "v142", .expect.invalid = true },
  { .name = "empty_component", .sdk = "10..0", .expect.invalid = true },
  { .name = "five_components", .sdk = "1.2.3.4.5", .expect.invalid = true },
};

sp_test_each(msvc_request, valid, test_t, tests) {
  bool valid = spn_msvc_toolset_valid(sp_cstr_as_str(it->toolset)) && spn_msvc_sdk_valid(sp_cstr_as_str(it->sdk));
  sp_expect_eq(t, !it->expect.invalid, valid);
  return SP_OK;
}
