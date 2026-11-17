#include "spn_test.h"

#include "external/net.h"

typedef enum {
  BUNDLE_UNSET,
  BUNDLE_FILE,
  BUNDLE_LINK,
  BUNDLE_MISSING,
} bundle_t;

typedef struct {
  spn_net_trust_kind_t kind;
  bool missing;
} expect_t;

typedef struct {
  const c8* name;
  bundle_t ca_bundle;
  bundle_t ssl_cert_file;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "ca_bundle_may_be_a_symlink",
    .ca_bundle = BUNDLE_LINK,
    .expect = { .kind = SPN_NET_TRUST_CONFIG },
  },
  {
    .name = "ssl_cert_file_may_be_a_symlink",
    .ssl_cert_file = BUNDLE_LINK,
    .expect = { .kind = SPN_NET_TRUST_ENV },
  },
  {
    .name = "ssl_cert_file_overrides_ca_bundle",
    .ca_bundle = BUNDLE_FILE,
    .ssl_cert_file = BUNDLE_FILE,
    .expect = { .kind = SPN_NET_TRUST_ENV },
  },
  {
    .name = "missing_ca_bundle_is_reported",
    .ca_bundle = BUNDLE_MISSING,
    .expect = { .kind = SPN_NET_TRUST_CONFIG, .missing = true },
  },
  {
    .name = "missing_ssl_cert_file_is_reported",
    .ssl_cert_file = BUNDLE_MISSING,
    .expect = { .kind = SPN_NET_TRUST_ENV, .missing = true },
  },
};

static sp_err_t place(sp_test_t* t, bundle_t bundle, sp_str_t name, sp_str_t target, sp_str_t* path) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t named = sp_fs_join_path(mem, test_dir_str(t), name);
  switch (bundle) {
    case BUNDLE_UNSET: {
      return SP_OK;
    }
    case BUNDLE_FILE: {
      sp_must_ok(t, sp_fs_write(named, sp_str_lit("A"), SP_FS_WRITE_TRUNCATE));
      break;
    }
    case BUNDLE_LINK: {
      sp_str_t linked = sp_fs_join_path(mem, test_dir_str(t), target);
      sp_must_ok(t, sp_fs_write(linked, sp_str_lit("A"), SP_FS_WRITE_TRUNCATE));
      sp_must_ok(t, sp_fs_create_sym_link(linked, named, SP_FS_KIND_FILE));
      break;
    }
    case BUNDLE_MISSING: {
      break;
    }
  }
  *path = named;
  return SP_OK;
}

sp_test_each(net, trust, test_t, tests) {
  sp_test_skip_on_win32();
  sp_str_t ca_bundle = sp_zero;
  sp_str_t ssl_cert_file = sp_zero;
  sp_try(place(t, it->ca_bundle, sp_str_lit("A"), sp_str_lit("C"), &ca_bundle));
  sp_try(place(t, it->ssl_cert_file, sp_str_lit("B"), sp_str_lit("D"), &ssl_cert_file));

  spn_net_trust_t trust = spn_net_trust_find(ca_bundle, ssl_cert_file);

  sp_expect_eq(t, (u32)trust.kind, (u32)it->expect.kind);
  sp_expect_str_eq(t, trust.bundle, it->expect.kind == SPN_NET_TRUST_ENV ? ssl_cert_file : ca_bundle);
  sp_expect_eq(t, trust.missing, it->expect.missing);
  return SP_OK;
}

sp_test(net, unset_trust_is_what_the_machine_has) {
  sp_test_skip_on_win32();
  spn_net_trust_t trust = spn_net_trust_find(sp_str_lit(""), sp_str_lit(""));

  sp_sys_file_meta_t meta = sp_zero;
  bool found = !sp_fs_get_path_metadata(trust.bundle, &meta) && meta.kind == SP_FS_KIND_FILE;
  sp_expect_eq(t, (u32)trust.kind, (u32)(found ? SPN_NET_TRUST_FOUND : SPN_NET_TRUST_NONE));
  sp_expect_eq(t, sp_str_empty(trust.bundle), !found);
  sp_expect(t, !trust.missing);
  return SP_OK;
}
