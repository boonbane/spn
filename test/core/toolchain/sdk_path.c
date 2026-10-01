#include "toolchain.h"

typedef struct {
  spn_path_check_t check;
  test_arg_t sdk;
} expect_t;

typedef struct {
  const c8* name;
  spn_toolchain_source_t source;
  spn_path_root_t base;
  const c8* str;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { "local_absolute",                     SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_NONE,    "/S",     .expect = { .sdk = { .path = "/S" } } },
  { "local_absolute_ignores_base",        SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_PROJECT, "/S",     .expect = { .sdk = { .path = "/S" } } },
  { "local_relative_under_base",          SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_PROJECT, "S",      .expect = { .sdk = { .path = "S", .root = SPN_PATH_ROOT_PROJECT } } },
  { "local_relative_without_base",        SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_NONE,    "S",      .expect = { .check = SPN_PATH_UNROOTED } },
  { "local_malformed",                    SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_PROJECT, "./S",    .expect = { .check = SPN_PATH_MALFORMED } },
  { "distribution_relative",              SPN_TOOLCHAIN_SOURCE_DISTRIBUTION, SPN_PATH_ROOT_NONE,    "S",      .expect = { .sdk = { .name = "S" } } },
  { "distribution_relative_ignores_base", SPN_TOOLCHAIN_SOURCE_DISTRIBUTION, SPN_PATH_ROOT_PROJECT, "S/T",    .expect = { .sdk = { .name = "S/T" } } },
  { "distribution_absolute_is_host",      SPN_TOOLCHAIN_SOURCE_DISTRIBUTION, SPN_PATH_ROOT_NONE,    "/S",     .expect = { .sdk = { .path = "/S" } } },
  { "distribution_malformed",             SPN_TOOLCHAIN_SOURCE_DISTRIBUTION, SPN_PATH_ROOT_NONE,    "S/../T", .expect = { .check = SPN_PATH_MALFORMED } },
  { "detected_relative",                  SPN_TOOLCHAIN_SOURCE_DETECTED,     SPN_PATH_ROOT_PROJECT, "S",      .expect = { .sdk = { .name = "S" } } },
};

sp_test_each(sdk_path, classify, test_t, tests) {
  spn_arg_t sdk = sp_zero;
  spn_path_check_t check = spn_toolchain_sdk_path(it->source, it->base, sp_cstr_as_str(it->str), &sdk);
  sp_must_eq(t, (u32)it->expect.check, (u32)check);
  return test_check_arg(t, sdk, it->expect.sdk);
}
