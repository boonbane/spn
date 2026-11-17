#include "spn_test.h"

#include "paths/paths.h"
#include "toolchain/provision.h"
#include "toolchain/toolchain.h"

typedef struct {
  spn_err_t err;
  spn_unpack_reason_t unpack;
  const c8* entry;
  const c8* file;
  bool present;
  u32 stored;
} expect_t;

typedef struct {
  const c8* name;
  const c8* archive;
  bool occupied;
  bool reclaim;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "xz_archive_installs",
    .archive = "file.tar.xz",
    .expect = { .file = "B", .stored = 1 },
  },
  {
    .name = "installed_artifact_is_present_to_the_next_claim",
    .archive = "file.tar.xz",
    .reclaim = true,
    .expect = { .present = true, .stored = 1 },
  },
  {
    .name = "unpack_failure_carries_its_reason_and_entry",
    .archive = "dotdot.tar.xz",
    .expect = { .err = SPN_ERR_TOOLCHAIN_EXTRACT, .unpack = SPN_UNPACK_UNSAFE, .entry = "A/../B" },
  },
  {
    .name = "archive_with_nothing_inside_is_empty",
    .archive = "loose.tar.xz",
    .expect = { .err = SPN_ERR_TOOLCHAIN_EXTRACT, .unpack = SPN_UNPACK_EMPTY },
  },
  {
    .name = "root_occupied_by_a_file_fails",
    .archive = "file.tar.xz",
    .occupied = true,
    .expect = { .err = SPN_ERR_TOOLCHAIN_EXTRACT, .stored = 1 },
  },
};

sp_test_each(provision, install, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_must_ok(t, sp_fs_create_dir_at(sp_path_join(mem, sp_test_dir(t), sp_str_lit("store"))));
  spn_path_roots_t roots = sp_zero;
  spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_TOOLCHAIN, sp_test_dir(t));
  spn_toolchain_store_t store = {
    .mem = mem,
    .roots = &roots,
  };

  sp_path_t archive = sp_path_from_str(test_repo_path(mem, sp_test_format(t, "test/core/toolchain/archives/{}", sp_fmt_cstr(it->archive))));
  sp_str_t bytes = sp_zero;
  sp_must_ok(t, sp_fs_read_at(mem, archive, &bytes));
  spn_artifact_t artifact = {
    .url = sp_test_format(t, "https://A.example.com/{}", sp_fmt_cstr(it->archive)),
    .sha256 = test_sha256_hex(mem, bytes),
  };

  sp_path_t root = spn_path_at(&roots, spn_toolchain_artifact_root(mem, artifact));
  if (it->occupied) {
    sp_must_ok(t, sp_fs_write_at(root, sp_str_lit("A"), SP_FS_WRITE_TRUNCATE));
  }

  spn_toolchain_claim_t claim = spn_toolchain_claim(&store, mem, sp_str_lit("A"), artifact);
  sp_must(t, !claim.present);
  sp_must_ok(t, sp_fs_copy_file_at(archive, spn_path_at(&roots, claim.tarball), SP_FS_ATOMIC_REPLACE));
  sp_expect_eq(t, (u32)it->expect.err, (u32)spn_toolchain_install(&store, &claim, sp_str_lit("A"), artifact.url));

  spn_err_union_t failure = sp_zero;
  sp_da(spn_event_t) errs = spn_test_drain_errs(mem);
  sp_expect_eq(t, it->expect.err ? 1u : 0u, (u32)sp_da_size(errs));
  if (!sp_da_empty(errs)) {
    failure = errs[0].err;
  }
  sp_expect_eq(t, (u32)it->expect.err, (u32)failure.kind);
  sp_expect_eq(t, (u32)it->expect.unpack, (u32)failure.artifact.unpack);
  sp_expect_str_eq_c(t, failure.artifact.entry, it->expect.entry);

  if (it->expect.file) {
    sp_sys_file_meta_t meta = sp_zero;
    sp_expect_ok(t, sp_fs_get_link_metadata_at(sp_path_join(mem, root, sp_cstr_as_str(it->expect.file)), &meta));
    sp_expect_eq(t, meta.kind, SP_FS_KIND_FILE);
  }
  if (it->reclaim) {
    spn_toolchain_claim_t again = spn_toolchain_claim(&store, mem, sp_str_lit("A"), artifact);
    sp_expect_eq(t, it->expect.present, again.present);
  }

  u32 stored = 0;
  sp_sys_dir_t dir = sp_zero;
  sp_must_ok(t, sp_fs_open_dir_at(spn_path_at(&roots, spn_path_from_id(SPN_DIR_ID_TOOLCHAIN_STORE)), &dir));
  SP_ALIGNED u8 buf [SP_SYS_DIR_MIN_BUF] = sp_zero;
  sp_fs_dir_it_t walk = sp_fs_dir_it(dir);
  sp_fs_dir_entry_t entry = sp_zero;
  while (sp_fs_dir_it_next(&walk, sp_mem_slice(buf, sizeof(buf)), &entry)) {
    stored++;
  }
  sp_fs_dir_it_close(&walk);
  sp_fs_close_dir(dir);
  sp_expect_eq(t, it->expect.stored, stored);

  spn_path_roots_close(&roots);
  return SP_OK;
}
