#include "spn_test.h"

#include "external/net.h"

typedef enum {
  BUNDLE_ABSENT,
  BUNDLE_FILE,
  BUNDLE_LINK,
} bundle_t;

typedef struct {
  bool named;
} expect_t;

typedef struct {
  const c8* name;
  bundle_t bundle;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "ssl_cert_file_names_the_bundle",
    .bundle = BUNDLE_FILE,
    .expect = { .named = true },
  },
  {
    .name = "ssl_cert_file_may_be_a_symlink",
    .bundle = BUNDLE_LINK,
    .expect = { .named = true },
  },
  {
    .name = "missing_ssl_cert_file_is_ignored",
  },
};

sp_test_each(net, trust, test_t, tests) {
  sp_test_skip_on_win32();
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t dir = test_dir_str(t);
  sp_str_t named = sp_fs_join_path(mem, dir, sp_str_lit("A"));
  sp_str_t target = sp_fs_join_path(mem, dir, sp_str_lit("B"));

  switch (it->bundle) {
    case BUNDLE_ABSENT: {
      break;
    }
    case BUNDLE_FILE: {
      sp_must_ok(t, sp_fs_create_file_str(named, sp_str_lit("A")));
      break;
    }
    case BUNDLE_LINK: {
      sp_must_ok(t, sp_fs_create_file_str(target, sp_str_lit("A")));
      sp_must_ok(t, sp_fs_create_sym_link(target, named, SP_FS_KIND_FILE));
      break;
    }
  }

  spn_net_trust_t trust = spn_net_trust_find(named);

  sp_expect_eq(t, sp_str_equal(trust.bundle, named), it->expect.named);
  return SP_OK;
}
