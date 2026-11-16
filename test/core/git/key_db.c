#include "git.h"

typedef struct {
  const c8* key;
} expect_t;

typedef struct {
  const c8* name;
  const c8* url;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "names_the_repo_and_hashes_the_url",
    .url = "https://A/B",
    .expect = { .key = "B-9520e3f54872d536" },
  },
  {
    .name = "same_name_elsewhere_is_another_db",
    .url = "https://D/B",
    .expect = { .key = "B-00f9e2b927e6f41c" },
  },
};

sp_test_each(git_key, db_key, test_t, tests) {
  sp_str_t key = spn_git_db_key(sp_test_arena(t), sp_cstr_as_str(it->url));
  sp_expect_str_eq_c(t, key, it->expect.key);
  return SP_OK;
}
