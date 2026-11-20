#include "spn_test.h"

#include "index/index.h"
#include "intern/intern.h"
#include "lock/lock.h"

typedef struct {
  const c8* rev;
} expect_t;

typedef struct {
  const c8* name;
  const c8* rev;
  struct { const c8* url; const c8* rev; } held;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "lock_holds_the_pin",
    .held = { .url = "U", .rev = "R" },
    .expect.rev = "R",
  },
  {
    .name = "declared_rev_outranks_the_lock",
    .rev = "S",
    .held = { .url = "U", .rev = "R" },
    .expect.rev = "S",
  },
  {
    .name = "pin_for_another_url_is_ignored",
    .held = { .url = "V", .rev = "R" },
  },
};

sp_test_each(index, pin, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_intern_str_t name = spn_intern_lit("I");

  spn_index_info_t index = {
    .id = name.id,
    .name = name,
    .git = { .url = sp_str_lit("U"), .rev = sp_cstr_as_str(it->rev ? it->rev : "") },
  };

  spn_lock_t lock = spn_lock_new(mem);
  spn_lock_pin_index(&lock, (spn_lock_index_t) {
    .name = name,
    .url = sp_cstr_as_str(it->held.url),
    .rev = sp_cstr_as_str(it->held.rev),
  });

  sp_expect_str_eq_c(t, si_index_pin(&index, &lock), it->expect.rev ? it->expect.rev : "");
  return SP_OK;
}
