#include "git.h"

typedef struct {
  const c8* name;
} expect_t;

typedef struct {
  const c8* name;
  const c8* url;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "https_git_suffix", .url = "https://A/B.git", .expect = { .name = "B" } },
  { .name = "https_no_suffix",  .url = "https://A/B",     .expect = { .name = "B" } },
  { .name = "ssh",              .url = "U@A:B",           .expect = { .name = "B" } },
  { .name = "trailing_slash",   .url = "https://A/B/",    .expect = { .name = "B" } },
};

sp_test_each(git_key, url_name, test_t, tests) {
  sp_str_t name = spn_git_url_name(sp_cstr_as_str(it->url));
  sp_expect_str_eq_c(t, name, it->expect.name);
  return SP_OK;
}
