#include "git.h"

typedef struct {
  const c8* url;
} expect_t;

typedef struct {
  const c8* name;
  const c8* url;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "scp",
    .url = "U@h:A/B.git",
    .expect = { .url = "https://h/A/B.git" },
  },
  {
    .name = "scp_absolute_path",
    .url = "U@h:/A/B.git",
    .expect = { .url = "https://h/A/B.git" },
  },
  {
    .name = "ssh",
    .url = "ssh://U@h/A/B.git",
    .expect = { .url = "https://h/A/B.git" },
  },
  {
    .name = "ssh_no_user",
    .url = "ssh://h/A/B.git",
    .expect = { .url = "https://h/A/B.git" },
  },
  {
    .name = "https",
    .url = "https://h/A/B.git",
  },
  {
    .name = "https_case",
    .url = "https://H/A/B.git",
    .expect = { .url = "https://h/A/B.git" },
  },
  {
    .name = "scheme_case",
    .url = "HTTPS://h/A/B.git",
    .expect = { .url = "https://h/A/B.git" },
  },
  {
    .name = "http",
    .url = "http://h/A/B.git",
  },
  {
    .name = "http_user",
    .url = "http://U@h/A/B.git",
    .expect = { .url = "http://h/A/B.git" },
  },
  {
    .name = "git_scheme",
    .url = "git://h/A/B.git",
  },
  {
    .name = "local_path",
    .url = "/A/B",
  },
  {
    .name = "colon_in_path",
    .url = "/A:B",
  },
  {
    .name = "ssh_port",
    .url = "ssh://U@h:2222/A/B.git",
  },
  {
    .name = "windows_drive",
    .url = "C:/A/B",
  },
  {
    .name = "no_path",
    .url = "U@h:",
  },
};

sp_test_each(git_url, web, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t web = spn_git_url_web(mem, sp_cstr_as_str(it->url));
  sp_expect_str_eq_c(t, web, it->expect.url ? it->expect.url : it->url);
  return SP_OK;
}
