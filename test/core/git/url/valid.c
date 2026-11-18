#include "../git.h"

typedef struct {
  bool valid;
} expect_t;

typedef struct {
  const c8* name;
  const c8* url;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "named",
    .url = "https://A/B",
    .expect = { .valid = true },
  },
  {
    .name = "empty",
    .url = "",
  },
  {
    .name = "only_a_separator",
    .url = "/",
  },
  {
    .name = "only_a_git_suffix",
    .url = ".git",
  },
  {
    .name = "current_dir",
    .url = "https://A/.",
  },
  {
    .name = "parent_dir",
    .url = "https://A/..",
  },
  {
    .name = "parent_dir_scp",
    .url = "U@A:..",
  },
};

sp_test_each(git_url, valid, test_t, tests) {
  sp_expect_eq(t, spn_git_url_valid(sp_cstr_as_str(it->url)), it->expect.valid);
  return SP_OK;
}
