#include "../compiler.h"

static const rsp_test_t tests [] = {
  {
    .name = "plain_unquoted",
    .args = { "-o", "A/B.exe", "C.o" },
    .expect = "-o\nA/B.exe\nC.o\n",
  },
  {
    .name = "whitespace_quoted",
    .args = { "A B" },
    .expect = "\"A B\"\n",
  },
  {
    .name = "apostrophe_quoted",
    .args = { "A'B" },
    .expect = "\"A'B\"\n",
  },
  {
    .name = "quote_escaped",
    .args = { "A\"B" },
    .expect = "\"A\\\"B\"\n",
  },
  {
    .name = "backslash_quoted_and_escaped",
    .args = { "A\\B" },
    .expect = "\"A\\\\B\"\n",
  },
  {
    .name = "trailing_backslash_escaped",
    .args = { "A\\" },
    .expect = "\"A\\\\\"\n",
  },
  {
    .name = "backslash_before_quote_both_escaped",
    .args = { "A\\\"B" },
    .expect = "\"A\\\\\\\"B\"\n",
  },
};

sp_test_each(rsp_gnu, render, rsp_test_t, tests, .setup = spn_test_ctx_setup) {
  return expect_rsp(t, it, SPN_RSP_STYLE_GNU);
}
