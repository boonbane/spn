#include "spn_test.h"

#include "gen.h"
#include "jtd.h"

#define UNION_TEST_MAX_KINDS 4

typedef struct {
  bool ok;
  const c8* guest [UNION_TEST_MAX_KINDS];
} expect_t;

typedef struct {
  const c8* name;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "guest_ascending", .expect = { .ok = true, .guest = { "2", "4" } } },
  { .name = "guest_descending" },
  { .name = "guest_after_host" },
  { .name = "guest_reserved" },
  { .name = "guest_unparsed" },
};

sp_test_each(gen_union, guest, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t path = test_repo_path(mem, sp_fmt(mem, "test/core/gen/schemas/{}.jtd.json", sp_fmt_cstr(it->name)).value);
  jtd_result_t jtd = sp_zero;
  sp_must_eq(t, JTD_OK, jtd_parse_file(mem, path, &jtd));

  gen_t* g = gen_new(mem);
  g->format = GEN_FORMAT_ERRORS;
  sp_must_eq(t, gen_lower_union(g, jtd.root), it->expect.ok);
  sp_expect_eq(t, sp_str_empty(g->err), it->expect.ok);
  if (!it->expect.ok) {
    return SP_OK;
  }

  u32 len = 0;
  sp_carr_detect_len(it->expect.guest, len, it->expect.guest[len]);
  sp_da_for(g->kinds, i) {
    sp_expect_str_eq_c(t, g->kinds[i].guest, i < len ? it->expect.guest[i] : "");
  }

  return SP_OK;
}
