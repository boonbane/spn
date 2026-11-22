#include "compiler.h"

#include "compiler/rsp.h"

#define rsp_args_max 8

typedef struct {
  const c8* rendered;
} expect_t;

typedef struct {
  const c8* name;
  spn_rsp_style_t style;
  const c8* args [rsp_args_max];
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "one_arg_per_line",
    .args = { "/nologo", "/OUT:A.lib", "B.o", "C.o" },
    .expect = { .rendered = "/nologo\n/OUT:A.lib\nB.o\nC.o\n" },
  },
  {
    .name = "whitespace_quoted",
    .args = { "A B", "C\tD" },
    .expect = { .rendered = "\"A B\"\n\"C\tD\"\n" },
  },
  {
    .name = "empty_quoted",
    .args = { "" },
    .expect = { .rendered = "\"\"\n" },
  },
  {
    .name = "apostrophe_quoted",
    .args = { "A'B" },
    .expect = { .rendered = "\"A'B\"\n" },
  },
  {
    .name = "hash_prefix_quoted",
    .args = { "#A" },
    .expect = { .rendered = "\"#A\"\n" },
  },
  {
    .name = "windows_quote_escaped",
    .args = { "A\"B" },
    .expect = { .rendered = "\"A\\\"B\"\n" },
  },
  {
    .name = "windows_backslashes_before_quote_doubled",
    .args = { "A\\\\\"B" },
    .expect = { .rendered = "\"A\\\\\\\\\\\"B\"\n" },
  },
  {
    .name = "windows_trailing_backslashes_doubled_when_quoted",
    .args = { "A B\\" },
    .expect = { .rendered = "\"A B\\\\\"\n" },
  },
  {
    .name = "windows_backslashes_kept_when_unquoted",
    .args = { "A\\B\\" },
    .expect = { .rendered = "A\\B\\\n" },
  },
  {
    .name = "gnu_quote_escaped",
    .style = SPN_RSP_STYLE_GNU,
    .args = { "A\"B" },
    .expect = { .rendered = "\"A\\\"B\"\n" },
  },
  {
    .name = "gnu_backslash_quoted_and_escaped",
    .style = SPN_RSP_STYLE_GNU,
    .args = { "A\\B" },
    .expect = { .rendered = "\"A\\\\B\"\n" },
  },
};

sp_test_each(rsp, render, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  sp_da(spn_arg_t) args = sp_da_new(mem, spn_arg_t);
  u32 len = 0;
  sp_carr_detect_len(it->args, len, it->args[len]);
  sp_for(at, len) {
    sp_da_push(args, spn_arg_lit(sp_cstr_as_str(it->args[at])));
  }

  sp_io_dyn_mem_writer_t buf = sp_zero;
  sp_io_dyn_mem_writer_init(mem, &buf);
  spn_rsp_render(&buf.base, &spn.roots, it->style, args);
  sp_expect_str_eq_c(t, sp_io_dyn_mem_writer_as_str(&buf), it->expect.rendered);
  return SP_OK;
}
