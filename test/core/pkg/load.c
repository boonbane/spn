#include "spn_test.h"

#include "intern/intern.h"
#include "paths/paths.h"
#include "pkg/load.h"

typedef struct {
  spn_err_t err;
  const c8* qualified;
  spn_err_t issue;
} expect_t;

typedef struct {
  const c8* name;
  const c8* manifest;
  const c8* request;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "loads",
    .manifest = "A",
    .request = "core/A",
    .expect = { .qualified = "core/A" },
  },
  {
    .name = "missing_file",
    .manifest = "Z",
    .request = "core/Z",
    .expect = { .err = SPN_ERR_NO_MANIFEST },
  },
  {
    .name = "issues",
    .manifest = "B",
    .request = "core/B",
    .expect = { .err = SPN_ERR_MANIFEST_ISSUES, .issue = SPN_ERR_CODEGEN_INVALID },
  },
  {
    .name = "patch_is_root_only",
    .manifest = "C",
    .request = "core/C",
    .expect = { .err = SPN_ERR_MANIFEST_ISSUES, .issue = SPN_ERR_CODEGEN_ROOT_ONLY },
  },
};

sp_test_each(pkg_load, cases, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t file = sp_fmt(mem, "{}.toml", sp_fmt_cstr(it->manifest)).value;
  spn_path_t path = { .sub = sp_fs_join_path(mem, test_repo_path(mem, sp_str_lit(PKG_MANIFEST_DIR)), file) };

  si_pkg_info_t pkg = sp_zero;
  spn_err_union_t err = sp_zero;
  sp_must_eq(t, it->expect.err, spn_pkg_load(mem, spn.intern, &spn.roots, sp_cstr_as_str(it->request), path, &pkg, &err));

  switch (it->expect.err) {
    case SPN_OK: {
      sp_expect_str_eq_c(t, pkg.qualified.str, it->expect.qualified);
      break;
    }
    case SPN_ERR_NO_MANIFEST: {
      sp_expect_eq(t, it->expect.err, err.kind);
      sp_expect_str_eq(t, err.no_manifest.path, path.sub);
      break;
    }
    case SPN_ERR_MANIFEST_ISSUES: {
      sp_expect_eq(t, it->expect.err, err.kind);
      sp_expect_str_eq_c(t, err.manifest.name, it->request);
      sp_expect_str_eq(t, err.manifest.path, path.sub);
      sp_must_eq(t, 1, sp_da_size(err.manifest.issues));
      sp_expect_eq(t, it->expect.issue, err.manifest.issues[0].code);
      break;
    }
    default: {
      break;
    }
  }

  return SP_OK;
}
