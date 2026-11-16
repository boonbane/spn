#include "git.h"

typedef struct {
  bool ssh;
} expect_t;

typedef struct {
  const c8* name;
  const c8* url;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "scp",
    .url = "U@H:A/B.git",
    .expect = { .ssh = true },
  },
  {
    .name = "ssh",
    .url = "ssh://U@H/A/B.git",
    .expect = { .ssh = true },
  },
  {
    .name = "git_ssh",
    .url = "git+ssh://U@H/A/B.git",
    .expect = { .ssh = true },
  },
  {
    .name = "ssh_git",
    .url = "ssh+git://U@H/A/B.git",
    .expect = { .ssh = true },
  },
  {
    .name = "https",
    .url = "https://H/A/B.git",
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
    .name = "windows_drive",
    .url = "C:/A/B",
  },
};

sp_test_each(git_url, ssh, test_t, tests) {
  sp_expect_eq(t, spn_git_url_is_ssh(sp_cstr_as_str(it->url)), it->expect.ssh);
  return SP_OK;
}
