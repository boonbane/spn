#include "spn_test.h"

#include "paths/paths.h"
#include "toolchain/provision.h"

typedef struct {
  const c8* name;
  const c8* pinned;
  const c8* download;
  spn_err_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "pinned_bytes_verify",
    .pinned = "A",
    .download = "A",
  },
  {
    .name = "other_bytes_are_a_sha_mismatch",
    .pinned = "A",
    .download = "B",
    .expect = SPN_ERR_TOOLCHAIN_SHA,
  },
  {
    .name = "missing_download_is_unreadable",
    .pinned = "A",
    .expect = SPN_ERR_TOOLCHAIN_READ,
  },
};

sp_test_each(verify, download, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  sp_must_ok(t, sp_fs_create_dir_at(sp_path_join(mem, sp_test_dir(t), sp_str_lit("store"))));
  spn_path_roots_t roots = sp_zero;
  spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_TOOLCHAIN, sp_test_dir(t));
  spn_toolchain_store_t store = {
    .mem = mem,
    .roots = &roots,
  };

  spn_artifact_t artifact = { .sha256 = test_sha256_hex(mem, sp_cstr_as_str(it->pinned)) };
  spn_toolchain_claim_t claim = spn_toolchain_claim(&store, mem, sp_str_lit("A"), artifact);
  if (it->download) {
    sp_must_ok(t, sp_fs_create_file_str_at(spn_path_at(&roots, claim.tarball), sp_cstr_as_str(it->download)));
  }

  sp_str_t actual = sp_zero;
  sp_expect_eq(t, (u32)spn_toolchain_verify(&store, &claim, mem, &actual), (u32)it->expect);
  if (it->download) {
    sp_expect_str_eq(t, actual, test_sha256_hex(mem, sp_cstr_as_str(it->download)));
  }

  spn_toolchain_release(&store, &claim);
  spn_path_roots_close(&roots);
  return SP_OK;
}
