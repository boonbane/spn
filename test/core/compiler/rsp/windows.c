#include "../compiler.h"

static const rsp_test_t tests [] = {
  {
    .name = "one_arg_per_line",
    .args = { "/nologo", "/OUT:A.lib", "B.o", "C.o" },
    .expect = "/nologo\n/OUT:A.lib\nB.o\nC.o\n",
  },
  {
    .name = "no_args",
    .expect = "",
  },
  {
    .name = "whitespace_quoted",
    .args = { "A B", "C\tD" },
    .expect = "\"A B\"\n\"C\tD\"\n",
  },
  {
    .name = "empty_quoted",
    .args = { "" },
    .expect = "\"\"\n",
  },
  {
    .name = "quote_escaped",
    .args = { "A\"B" },
    .expect = "\"A\\\"B\"\n",
  },
  {
    .name = "backslashes_before_quote_doubled",
    .args = { "A\\\\\"B" },
    .expect = "\"A\\\\\\\\\\\"B\"\n",
  },
  {
    .name = "trailing_backslashes_doubled_when_quoted",
    .args = { "A B\\" },
    .expect = "\"A B\\\\\"\n",
  },
  {
    .name = "apostrophe_quoted",
    .args = { "A'B" },
    .expect = "\"A'B\"\n",
  },
  {
    .name = "hash_prefix_quoted",
    .args = { "#A" },
    .expect = "\"#A\"\n",
  },
  {
    .name = "newline_quoted",
    .args = { "A\nB" },
    .expect = "\"A\nB\"\n",
  },
  {
    .name = "backslashes_kept_when_unquoted",
    .args = { "A\\B\\" },
    .expect = "A\\B\\\n",
  },
};

sp_test_each(rsp_windows, render, rsp_test_t, tests, .setup = spn_test_ctx_setup) {
  return expect_rsp(t, it, SPN_RSP_STYLE_WINDOWS);
}
