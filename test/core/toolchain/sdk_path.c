#include "toolchain.h"

typedef struct {
  spn_err_t err;
  test_arg_t arg;
} expect_t;

typedef struct {
  const c8* name;
  spn_toolchain_source_t source;
  spn_path_root_t base;
  const c8* str;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { "local_absolute",                     SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_NONE,    "/S",     .expect = { .arg = { .path = "/S" } } },
  { "local_absolute_ignores_base",        SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_PROJECT, "/S",     .expect = { .arg = { .path = "/S" } } },
  { "local_relative_under_base",          SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_PROJECT, "S",      .expect = { .arg = { .path = "S", .root = SPN_PATH_ROOT_PROJECT } } },
  { "local_relative_without_base",        SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_NONE,    "S",      .expect = { .err = SPN_ERR_CODEGEN_UNROOTED } },
  { "local_malformed",                    SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_PROJECT, "./S",    .expect = { .err = SPN_ERR_CODEGEN_PATH } },
  { "local_empty",                        SPN_TOOLCHAIN_SOURCE_LOCAL,        SPN_PATH_ROOT_PROJECT, "",       .expect = { .err = SPN_ERR_CODEGEN_PATH } },
  { "distribution_relative",              SPN_TOOLCHAIN_SOURCE_DISTRIBUTION, SPN_PATH_ROOT_NONE,    "S",      .expect = { .arg = { .name = "S" } } },
  { "distribution_relative_ignores_base", SPN_TOOLCHAIN_SOURCE_DISTRIBUTION, SPN_PATH_ROOT_PROJECT, "S/T",    .expect = { .arg = { .name = "S/T" } } },
  { "distribution_absolute_is_host",      SPN_TOOLCHAIN_SOURCE_DISTRIBUTION, SPN_PATH_ROOT_NONE,    "/S",     .expect = { .arg = { .path = "/S" } } },
  { "distribution_malformed",             SPN_TOOLCHAIN_SOURCE_DISTRIBUTION, SPN_PATH_ROOT_NONE,    "S/../T", .expect = { .err = SPN_ERR_CODEGEN_PATH } },
};

sp_test_each(sdk_path, classify, test_t, tests) {
  spn_arg_t arg = sp_zero;
  spn_err_t err = si_toolchain_path(it->source, it->base, sp_cstr_as_str(it->str), &arg);
  sp_must_eq(t, (u32)it->expect.err, (u32)err);
  return test_check_arg(t, arg, it->expect.arg);
}
