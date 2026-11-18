#include "index.h"

#include "when/when.h"

#define DIR_TEST_MAX_DEPS 4
#define DIR_TEST_MAX_TARGETS 2
#define DIR_TEST_MAX_OPTIONS 2
#define DIR_TEST_MAX_LINKAGES 4

typedef struct {
  bool exists;
  spn_err_t err;
  spn_semver_t version;
  struct {
    const c8* name;
    const c8* range;
    spn_index_dep_kind_t kind;
    bool public;
    const c8* when;
    const c8* options;
  } deps [DIR_TEST_MAX_DEPS];
  struct { const c8* name; spn_linkage_t linkages [DIR_TEST_MAX_LINKAGES]; } targets [DIR_TEST_MAX_TARGETS];
  struct { const c8* name; spn_option_type_t type; u32 defaults; } options [DIR_TEST_MAX_OPTIONS];
  const c8* dep;
} dir_expect_t;

typedef struct {
  const c8* name;
  const c8* package;
  const c8* toml;
  dir_expect_t expect;
} dir_test_t;

static const dir_test_t tests [] = {
  {
    .name = "release",
    .package = "A",
    .toml = "minimal",
    .expect = {
      .exists = true,
      .version = { 1, 2, 3 },
    },
  },
  {
    .name = "deps",
    .package = "A",
    .toml = "deps",
    .expect = {
      .exists = true,
      .version = { 1, 0, 0 },
      .deps = {
        { .name = "B", .range = "^1.0.0", .public = true, .when = "os = \"linux\"", .options = "O = true" },
        { .name = "C", .range = "^2.0.0" },
      },
    },
  },
  {
    .name = "dep_kinds",
    .package = "A",
    .toml = "kinds",
    .expect = {
      .exists = true,
      .version = { 1, 0, 0 },
      .deps = {
        { .name = "B", .range = "^1.0.0" },
        { .name = "C", .range = "^1.0.0", .kind = SPN_INDEX_DEP_BUILD },
      },
    },
  },
  {
    .name = "project_deps",
    .package = "A",
    .toml = "project_deps",
    .expect = {
      .exists = true,
      .version = { 1, 0, 0 },
      .deps = {
        { .name = "B", .range = "^1.0.0" },
      },
    },
  },
  {
    .name = "targets",
    .package = "A",
    .toml = "targets",
    .expect = {
      .exists = true,
      .version = { 1, 0, 0 },
      .targets = {
        { .name = "A", .linkages = { SPN_LIB_KIND_STATIC, SPN_LIB_KIND_SHARED } },
      },
    },
  },
  {
    .name = "options",
    .package = "A",
    .toml = "options",
    .expect = {
      .exists = true,
      .version = { 1, 0, 0 },
      .options = {
        { .name = "O", .type = SPN_OPTION_TYPE_BOOL, .defaults = 1 },
      },
    },
  },
  {
    .name = "missing",
  },
  {
    .name = "empty_dir",
    .package = "A",
  },
  {
    .name = "broken_manifest",
    .package = "A",
    .toml = "broken",
    .expect = {
      .err = SPN_ERR_MANIFEST_ISSUES,
    },
  },
  {
    .name = "name_mismatch",
    .package = "A",
    .toml = "renamed",
    .expect = {
      .err = SPN_ERR_INDEX_CORRUPT,
    },
  },
  {
    .name = "path_dep",
    .package = "A",
    .toml = "path_dep",
    .expect = {
      .err = SPN_ERR_INDEX_PATH_DEP,
      .dep = "B",
    },
  },
};

sp_test_each(index_dir, get_package, dir_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t location = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("index"));
  sp_fs_create_dir(location);

  sp_str_t package = sp_zero;
  sp_str_t manifest = sp_zero;
  if (it->package) {
    package = sp_fs_join_path(mem, location, sp_cstr_as_str(it->package));
    manifest = sp_fs_join_path(mem, package, sp_str_lit("spn.toml"));
    sp_fs_create_dir(package);
    if (it->toml) {
      sp_str_t fixture = test_repo_path(mem, sp_test_format(t, "test/core/index/packages/{}.toml", sp_fmt_cstr(it->toml)));
      sp_str_t toml = sp_zero;
      sp_fs_read(mem, fixture, &toml);
      sp_fs_write(manifest, toml, SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms);
    }
  }

  spn_index_info_t index = {
    .protocol = SPN_INDEX_PROTOCOL_DIR,
    .location = { .at = sp_path_from_str(location), .dir = location },
  };

  sp_intern_str_t request = spn_intern_lit("A");

  spn_index_pkg_t* pkg = SP_NULLPTR;
  spn_err_union_t diag = sp_zero;
  spn_err_t err = spn_index_get_package(&index, mem, spn.intern, &spn.roots, request, &pkg, &diag);

  sp_expect_eq(t, it->expect.err, err);
  sp_must_eq(t, it->expect.exists, pkg != SP_NULLPTR);

  switch (err) {
    case SPN_ERR_INDEX_CORRUPT: {
      sp_expect_str_eq(t, diag.index_corrupt.path, manifest);
      break;
    }
    case SPN_ERR_MANIFEST_ISSUES: {
      sp_expect_str_eq(t, diag.manifest.path, manifest);
      break;
    }
    case SPN_ERR_INDEX_PATH_DEP: {
      sp_expect_str_eq_c(t, diag.pkg.requested, it->expect.dep);
      break;
    }
    default: {
      break;
    }
  }

  if (pkg) {
    sp_must_eq(t, 1, sp_da_size(pkg->releases));
    spn_index_release_t* release = &pkg->releases[0];

    sp_expect(t, spn_semver_eq(it->expect.version, release->version));
    sp_expect_str_eq(t, release->name.str, request.str);
    sp_expect_str_eq_c(t, release->paths.manifest, "spn.toml");

    sp_must_eq(t, SPN_PKG_ROOT_LOCAL, release->source.kind);
    sp_expect_str_eq(t, release->source.local.sub, package);

    u32 deps = 0;
    sp_carr_detect_len(it->expect.deps, deps, it->expect.deps[deps].name);
    sp_must_eq(t, deps, sp_da_size(release->deps));
    sp_for(at, deps) {
      sp_expect_str_eq_c(t, release->deps[at].name.str, it->expect.deps[at].name);
      sp_expect_str_eq_c(t, spn_semver_range_to_str(mem, release->deps[at].range), it->expect.deps[at].range);
      sp_expect_eq(t, it->expect.deps[at].kind, release->deps[at].kind);
      sp_expect_eq(t, it->expect.deps[at].public, release->deps[at].public);
      sp_expect_str_eq_c(t, spn_when_to_str(mem, &release->deps[at].when), it->expect.deps[at].when ? it->expect.deps[at].when : "always");
      sp_expect_str_eq_c(t, spn_when_to_str(mem, &release->deps[at].options), it->expect.deps[at].options ? it->expect.deps[at].options : "always");
    }

    u32 targets = 0;
    sp_carr_detect_len(it->expect.targets, targets, it->expect.targets[targets].name);
    sp_must_eq(t, targets, sp_da_size(release->targets));
    sp_for(at, targets) {
      spn_index_target_t* target = &release->targets[at];
      sp_expect_str_eq_c(t, target->name, it->expect.targets[at].name);

      u32 linkages = 0;
      sp_carr_detect_len(it->expect.targets[at].linkages, linkages, it->expect.targets[at].linkages[linkages]);
      sp_must_eq(t, linkages, sp_da_size(target->linkages));
      sp_for(kind, linkages) {
        sp_expect_eq(t, it->expect.targets[at].linkages[kind], target->linkages[kind]);
      }
    }

    u32 options = 0;
    sp_carr_detect_len(it->expect.options, options, it->expect.options[options].name);
    sp_must_eq(t, options, si_om_size(release->options));
    sp_for(at, options) {
      spn_option_info_t* option = si_om_get(release->options, spn_intern(sp_cstr_as_str(it->expect.options[at].name)).id);
      sp_must(t, option != SP_NULLPTR);
      sp_expect_eq(t, it->expect.options[at].type, option->type);
      sp_expect_eq(t, it->expect.options[at].defaults, si_da_size(option->defaults));
    }
  }

  return SP_OK;
}

#define versioned_max_dirs 3

typedef struct {
  spn_err_t err;
  const c8* path;
  struct { const c8* dir; spn_semver_t version; } releases [versioned_max_dirs];
} versioned_expect_t;

typedef struct {
  const c8* name;
  struct { const c8* dir; const c8* toml; } dirs [versioned_max_dirs];
  versioned_expect_t expect;
} versioned_test_t;

static const versioned_test_t versioned_tests [] = {
  {
    .name = "release",
    .dirs = {
      { .dir = "A/B", .toml = "minimal" },
    },
    .expect = {
      .releases = {
        { .dir = "A/B", .version = { 1, 2, 3 } },
      },
    },
  },
  {
    .name = "release_per_dir",
    .dirs = {
      { .dir = "A/B", .toml = "minimal" },
      { .dir = "A/C", .toml = "newer" },
      { .dir = "A/D", .toml = "older" },
    },
    .expect = {
      .releases = {
        { .dir = "A/D", .version = { 1, 0, 0 } },
        { .dir = "A/B", .version = { 1, 2, 3 } },
        { .dir = "A/C", .version = { 2, 0, 0 } },
      },
    },
  },
  {
    .name = "bare_dir",
    .dirs = {
      { .dir = "A/B", .toml = "minimal" },
      { .dir = "A/C" },
    },
    .expect = {
      .releases = {
        { .dir = "A/B", .version = { 1, 2, 3 } },
      },
    },
  },
  {
    .name = "flat_wins",
    .dirs = {
      { .dir = "A", .toml = "minimal" },
      { .dir = "A/B", .toml = "renamed" },
    },
    .expect = {
      .releases = {
        { .dir = "A", .version = { 1, 2, 3 } },
      },
    },
  },
  {
    .name = "broken_manifest",
    .dirs = {
      { .dir = "A/B", .toml = "minimal" },
      { .dir = "A/C", .toml = "broken" },
    },
    .expect = {
      .err = SPN_ERR_MANIFEST_ISSUES,
      .path = "A/C/spn.toml",
    },
  },
  {
    .name = "name_mismatch",
    .dirs = {
      { .dir = "A/B", .toml = "renamed" },
    },
    .expect = {
      .err = SPN_ERR_INDEX_CORRUPT,
      .path = "A/B/spn.toml",
    },
  },
  {
    .name = "duplicate_version",
    .dirs = {
      { .dir = "A/B", .toml = "minimal" },
      { .dir = "A/C", .toml = "minimal" },
    },
    .expect = {
      .err = SPN_ERR_INDEX_CORRUPT,
      .path = "A",
    },
  },
};

sp_test_each(index_dir, versioned, versioned_test_t, versioned_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t location = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("index"));

  u32 dirs = 0;
  sp_carr_detect_len(it->dirs, dirs, it->dirs[dirs].dir);
  sp_for(at, dirs) {
    sp_str_t dir = sp_fs_join_path(mem, location, sp_cstr_as_str(it->dirs[at].dir));
    sp_must_ok(t, sp_fs_create_dir(dir));
    if (it->dirs[at].toml) {
      sp_must_ok(t, sp_fs_copy_file_at(
        sp_path_from_str(test_repo_path(mem, sp_test_format(t, "test/core/index/packages/{}.toml", sp_fmt_cstr(it->dirs[at].toml)))),
        sp_path_from_str(sp_fs_join_path(mem, dir, sp_str_lit("spn.toml"))),
        SP_FS_WRITE_REPLACE));
    }
  }

  spn_index_info_t index = {
    .protocol = SPN_INDEX_PROTOCOL_DIR,
    .location = { .at = sp_path_from_str(location), .dir = location },
  };

  spn_index_pkg_t* pkg = SP_NULLPTR;
  spn_err_union_t diag = sp_zero;
  spn_err_t err = spn_index_get_package(&index, mem, spn.intern, &spn.roots, spn_intern_lit("A"), &pkg, &diag);

  sp_expect_eq(t, it->expect.err, err);

  switch (err) {
    case SPN_ERR_INDEX_CORRUPT: {
      sp_expect_str_eq(t, diag.index_corrupt.path, sp_fs_join_path(mem, location, sp_cstr_as_str(it->expect.path)));
      break;
    }
    case SPN_ERR_MANIFEST_ISSUES: {
      sp_expect_str_eq(t, diag.manifest.path, sp_fs_join_path(mem, location, sp_cstr_as_str(it->expect.path)));
      break;
    }
    default: {
      break;
    }
  }

  u32 releases = 0;
  sp_carr_detect_len(it->expect.releases, releases, it->expect.releases[releases].dir);
  sp_must_eq(t, releases, pkg ? sp_da_size(pkg->releases) : 0);
  sp_for(at, releases) {
    spn_index_release_t* release = &pkg->releases[at];

    sp_expect(t, spn_semver_eq(it->expect.releases[at].version, release->version));
    sp_must_eq(t, SPN_PKG_ROOT_LOCAL, release->source.kind);
    sp_expect_str_eq(t, release->source.local.sub, sp_fs_join_path(mem, location, sp_cstr_as_str(it->expect.releases[at].dir)));
  }

  return SP_OK;
}
