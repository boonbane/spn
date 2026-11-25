#include "compiler.h"

#include "compiler/stdinc.h"

#define search_list_args_max 8

typedef struct {
  spn_err_t err;
  const c8* args [search_list_args_max];
} expect_t;

typedef struct {
  const c8* name;
  const c8* builtin;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "builtin_splits_the_list",
    .builtin = "/C",
    .expect = {
      .args = { "-nostdlibinc", "-isystem/A", "-isystem/B", "-idirafter/D" },
    },
  },
  {
    .name = "builtin_unlisted",
    .builtin = "include",
    .expect = {
      .args = { "-nostdinc", "-isystem/A", "-isystem/B", "-isystem/C" },
    },
  },
  {
    .name = "framework",
    .builtin = "/A",
    .expect = {
      .args = { "-nostdlibinc", "-idirafter/B", "-iframework/F" },
    },
  },
  {
    .name = "unterminated",
    .expect = {
      .err = SPN_ERROR,
    },
  },
};

sp_test_each(search_list, args, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t path = test_repo_path(mem, sp_test_format(t, "test/core/compiler/search_list/{}.txt", sp_fmt_cstr(it->name)));
  sp_str_t content = sp_zero;
  sp_must(t, !sp_fs_read(mem, path, &content));

  si_da(sp_str_t) args = sp_zero;
  spn_err_t err = si_cc_parse_search_list(mem, content, sp_cstr_as_str(it->builtin), &args);

  sp_must_eq(t, it->expect.err, err);
  if (!err) {
    sp_must_strs_eq(t, args, si_da_size(args), it->expect.args);
  }
  return SP_OK;
}
