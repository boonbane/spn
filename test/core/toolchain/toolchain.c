#include "toolchain.h"

typedef struct {
  spn_toolchain_ref_kind_t kind;
  const c8* name;
} ref_expect_t;

typedef struct {
  const c8* name;
  const c8* str;
  ref_expect_t expect;
} ref_test_t;

static const ref_test_t ref_tests [] = {
  { .name = "empty_is_unset", .str = "", .expect = { .name = "" } },
  { .name = "auto_is_automatic", .str = "auto", .expect = { .kind = SPN_TOOLCHAIN_REF_AUTO, .name = "" } },
  { .name = "anything_else_is_named", .str = "A", .expect = { .kind = SPN_TOOLCHAIN_REF_NAMED, .name = "A" } },
};

sp_test_each(launcher, ref, ref_test_t, ref_tests, .setup = spn_test_ctx_setup) {
  spn_toolchain_ref_t ref = spn_toolchain_ref_from_str(sp_cstr_as_str(it->str));
  sp_expect_eq(t, (u32)it->expect.kind, (u32)ref.kind);
  sp_expect_str_eq_c(t, ref.name.str, it->expect.name);
  return SP_OK;
}

sp_test(launcher, root_prefixes_program) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  spn_toolchain_launcher_t launcher = { .program = spn_arg_lit(sp_str_lit("B/A")) };
  spn_toolchain_launcher_t rooted = spn_toolchain_launcher_with_root(mem, launcher, (spn_path_t) { .sub = sp_str_lit("/R") });
#if defined(SP_WIN32)
  sp_expect_str_eq_c(t, spn_arg_str(&roots, mem, rooted.program), "/R/B/A.exe");
#else
  sp_expect_str_eq_c(t, spn_arg_str(&roots, mem, rooted.program), "/R/B/A");
#endif
  return SP_OK;
}
