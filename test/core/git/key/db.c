#include "../git.h"

typedef struct {
  const c8* name;
  const c8* a;
  const c8* b;
} test_t;

static const test_t tests [] = {
  {
    .name = "same_name_elsewhere_is_another_db",
    .a = "https://A/B",
    .b = "https://D/B",
  },
};

sp_test_each(git_key, db_key, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t ka = spn_git_db_key(mem, sp_cstr_as_str(it->a));
  sp_str_t kb = spn_git_db_key(mem, sp_cstr_as_str(it->b));
  sp_expect(t, !sp_str_equal(ka, kb));
  return SP_OK;
}
