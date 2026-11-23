#include "compiler.h"

typedef struct {
  const c8* out;
} expect_t;

typedef struct {
  const c8* name;
  const c8* echo;
  const c8* out;
  const c8* err;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "first_line_is_dropped", .out = "A\nB\n", .expect = { .out = "B\n" } },
  { .name = "lone_echo_is_silent", .out = "A\n" },
  { .name = "stderr_leads_stdout", .out = "A\nB\n", .err = "E\n", .expect = { .out = "E\nB\n" } },
  { .name = "stderr_survives_alone", .err = "E\n", .expect = { .out = "E\n" } },
  { .name = "prefix_selects_the_line", .echo = " P ", .out = "B\n P A\nC\n", .expect = { .out = "B\nC\n" } },
  { .name = "unmatched_prefix_keeps_everything", .echo = " P ", .out = "B\n", .expect = { .out = "B\n" } },
};

sp_test_each(invocation, echo, test_t, tests) {
  sp_str_t out = spn_invocation_drop_echo(sp_test_arena(t), sp_cstr_as_str(it->echo), sp_cstr_as_str(it->out), sp_cstr_as_str(it->err));
  sp_expect_str_eq_c(t, out, it->expect.out);
  return SP_OK;
}
