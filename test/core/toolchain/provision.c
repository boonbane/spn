#include "toolchain.h"

#define PROVISION_MAX_TOOLCHAINS 2

typedef enum {
  PROVISION_TARBALL_TREE,
  PROVISION_TARBALL_GARBAGE,
  PROVISION_TARBALL_LOOSE,
} provision_tarball_t;

typedef struct {
  spn_err_t kind;
  u32 installs;
  bool extracted;
  bool store_clean;
  bool err_reports_sha;
} provision_expect_t;

typedef struct {
  const c8* name;
  const c8* toolchains [PROVISION_MAX_TOOLCHAINS];
  provision_tarball_t tarball;
  const c8* sha;
  bool vanish;
  bool dest_file;
  provision_expect_t expect;
} provision_test_t;

typedef struct {
  const c8* name;
  const c8* url;
  const c8* mirror;
  const c8* expect;
} resolve_test_t;

static const provision_test_t tests [] = {
  {
    .name = "fresh_artifact_installs",
    .expect = {
      .installs = 1,
      .extracted = true,
    },
  },
  {
    .name = "artifacts_share_store_by_sha",
    .toolchains = { "A", "B" },
    .expect = {
      .installs = 1,
      .extracted = true,
    },
  },
  {
    .name = "sha_mismatch_fails_and_leaves_no_store_entry",
    .sha = "beefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeefbeef",
    .expect = {
      .kind = SPN_ERR_TOOLCHAIN_SHA,
      .installs = 1,
      .store_clean = true,
      .err_reports_sha = true,
    },
  },
  {
    .name = "corrupt_archive_fails_and_leaves_no_store_entry",
    .tarball = PROVISION_TARBALL_GARBAGE,
    .expect = {
      .kind = SPN_ERR_TOOLCHAIN_EXTRACT,
      .installs = 1,
      .store_clean = true,
    },
  },
  {
    .name = "vanished_download_is_a_read_failure",
    .vanish = true,
    .expect = {
      .kind = SPN_ERR_TOOLCHAIN_READ,
      .installs = 1,
      .store_clean = true,
    },
  },
  {
    .name = "single_file_archive_fails_extract",
    .tarball = PROVISION_TARBALL_LOOSE,
    .expect = {
      .kind = SPN_ERR_TOOLCHAIN_EXTRACT,
      .installs = 1,
      .store_clean = true,
    },
  },
  {
    .name = "dest_occupied_by_file_fails",
    .dest_file = true,
    .expect = {
      .kind = SPN_ERR_TOOLCHAIN_EXTRACT,
      .installs = 1,
    },
  },
};

static const resolve_test_t resolve_tests [] = {
  {
    .name = "mirror_rewrites_url",
    .url = "https://tc.example.com/x/A.tar.gz",
    .mirror = "https://mirror.example.com/M",
    .expect = "https://mirror.example.com/M/A.tar.gz",
  },
  {
    .name = "mirror_trailing_slash",
    .url = "https://tc.example.com/x/A.tar.gz",
    .mirror = "https://mirror.example.com/M/",
    .expect = "https://mirror.example.com/M/A.tar.gz",
  },
  {
    .name = "empty_mirror_is_identity",
    .url = "https://tc.example.com/x/A.tar.gz",
    .mirror = "",
    .expect = "https://tc.example.com/x/A.tar.gz",
  },
  {
    .name = "url_without_file_is_unchanged",
    .url = "https://tc.example.com/",
    .mirror = "https://mirror.example.com/M",
    .expect = "https://tc.example.com/",
  },
};

static void provision_create(sp_mem_t mem, sp_str_t dir, const c8* rel, const c8* content) {
  sp_str_t path = sp_fs_join_path(mem, dir, sp_cstr_as_str(rel));
  sp_fs_create_dir(sp_fs_parent_path(path));
  sp_fs_create_file_str(path, sp_cstr_as_str(content));
}

sp_test_each(provision, store, provision_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t dir = test_dir_str(t);
  sp_str_t tarball = sp_fs_join_path(mem, dir, sp_str_lit("A.tar.gz"));

  switch (it->tarball) {
    case PROVISION_TARBALL_TREE: {
      provision_create(mem, dir, "tree/A/B", "B");
      provision_create(mem, dir, "tree/A/lib/C", "C");
      sp_ps_output_t tar = sp_ps_run(mem, (sp_ps_config_t) {
        .command = sp_str_lit("tar"),
        .args = {
          sp_str_lit("czf"), tarball,
          sp_str_lit("-C"), sp_fs_join_path(mem, dir, sp_str_lit("tree")),
          sp_str_lit("A"),
        }
      });
      sp_must_eq(t, 0, tar.status.exit_code);
      break;
    }
    case PROVISION_TARBALL_GARBAGE: {
      provision_create(mem, dir, "A.tar.gz", "A");
      break;
    }
    case PROVISION_TARBALL_LOOSE: {
      provision_create(mem, dir, "B.txt", "B");
      sp_ps_output_t tar = sp_ps_run(mem, (sp_ps_config_t) {
        .command = sp_str_lit("tar"),
        .args = {
          sp_str_lit("czf"), tarball,
          sp_str_lit("-C"), dir,
          sp_str_lit("B.txt"),
        }
      });
      sp_must_eq(t, 0, tar.status.exit_code);
      break;
    }
  }

  sp_str_t bytes = sp_zero;
  sp_must_ok(t, sp_io_read_file(mem, tarball, &bytes));
  sp_str_t sha = test_sha256_hex(mem, bytes);
  sp_must_eq(t, 64u, sha.len);

  sp_path_t store_dir = sp_path_join(mem, sp_test_dir(t), sp_str_lit("store"));
  sp_must_ok(t, sp_fs_create_dir_at(store_dir));
  spn_path_roots_t roots = sp_zero;
  spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_TOOLCHAIN, sp_test_dir(t));
  spn_toolchain_store_t store = {
    .mem = mem,
    .roots = &roots,
  };

  spn_artifact_t artifact = {
    .url = sp_fmt(mem, "https://tc.example.com/{}", sp_fmt_str(sp_fs_get_name(tarball))).value,
    .sha256 = it->sha ? sp_cstr_as_str(it->sha) : sha,
  };

  if (it->dest_file) {
    sp_fs_create_file_str_at(spn_path_at(&roots, spn_toolchain_artifact_root(mem, artifact)), sp_str_lit("A"));
  }

  spn_err_union_t payload = sp_zero;
  u32 installs = 0;

  sp_carr_for(it->toolchains, at) {
    const c8* name = it->toolchains[at];
    if (!name && !at) {
      name = "A";
    }
    if (!name) {
      break;
    }

    spn_err_t err = SPN_OK;
    spn_toolchain_claim_t claim = spn_toolchain_claim(&store, mem, artifact);
    if (!claim.present) {
      installs++;
      if (!it->vanish) {
        sp_must_ok(t, sp_fs_copy_file_at(sp_path_from_str(tarball), spn_path_at(&roots, claim.tarball), SP_FS_ATOMIC_REPLACE));
      }
      err = spn_toolchain_install(&store, &claim, sp_cstr_as_str(name), artifact.url);
    }
    sp_must_eq(t, (u32)it->expect.kind, (u32)err);
    if (err) {
      sp_da(spn_event_t) errs = spn_test_drain_errs(mem);
      sp_must_eq(t, 1, sp_da_size(errs));
      payload = errs[0].err;
      sp_expect_eq(t, payload.kind, err);
      sp_expect_str_eq_c(t, payload.artifact.name, name);
    }
  }

  sp_expect_eq(t, it->expect.installs, installs);

  if (it->expect.extracted) {
    sp_path_t root = spn_path_at(&roots, spn_toolchain_artifact_root(mem, artifact));
    sp_expect(t, sp_fs_is_dir_at(root));
    sp_expect(t, sp_fs_is_file_at(sp_path_join(mem, root, sp_str_lit("B"))));
    sp_expect(t, sp_fs_is_file_at(sp_path_join(mem, root, sp_str_lit("lib/C"))));
  }
  if (it->expect.store_clean) {
    sp_str_t lock = sp_fmt(mem, "{}.lock", sp_fmt_str(artifact.sha256)).value;
    u32 entries = 0;
    sp_fs_it_t walk = sp_fs_it_new_at(mem, spn_path_at(&roots, spn_path_from_id(SPN_DIR_ID_TOOLCHAIN_STORE)), 0);
    while (sp_fs_it_next(&walk)) {
      entries++;
      sp_expect_str_eq(t, walk.entry.name, lock);
    }
    sp_fs_it_deinit(&walk);
    sp_must_eq(t, 1u, entries);
  }
  spn_path_roots_close(&roots);
  if (it->expect.err_reports_sha) {
    sp_expect_str_eq(t, payload.artifact.expected, artifact.sha256);
    sp_expect_str_eq(t, payload.artifact.actual, sha);
  }

  return SP_OK;
}

sp_test_each(provision, resolve_url, resolve_test_t, resolve_tests) {
  sp_mem_t mem = sp_test_arena(t);

  spn_artifact_t artifact = {
    .url = sp_cstr_as_str(it->url),
    .sha256 = sp_str_lit("aa"),
  };
  sp_expect_str_eq_c(t, spn_artifact_resolve_url(mem, artifact, sp_cstr_as_str(it->mirror)), it->expect);

  return SP_OK;
}
