#include "index.h"

#include "index/marker.h"

typedef struct {
  spn_err_t err;
  spn_err_t issue;
  u64 version;
  spn_index_format_t format;
  const c8* path;
} expect_t;

typedef struct {
  const c8* name;
  const c8* dir;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "absent",
    .dir = "none",
  },
  {
    .name = "manifests",
    .dir = "manifests",
    .expect = { .version = 1, .format = SPN_INDEX_FORMAT_MANIFESTS, .path = "" },
  },
  {
    .name = "releases_under_a_path",
    .dir = "releases",
    .expect = { .version = 1, .format = SPN_INDEX_FORMAT_RELEASES, .path = "A/B" },
  },
  {
    .name = "path_is_normalized",
    .dir = "dotted",
    .expect = { .version = 1, .format = SPN_INDEX_FORMAT_MANIFESTS, .path = "A" },
  },
  {
    .name = "unknown_key_is_ignored",
    .dir = "unknown_key",
    .expect = { .version = 1, .format = SPN_INDEX_FORMAT_MANIFESTS, .path = "" },
  },
  {
    .name = "newer_version",
    .dir = "newer_version",
    .expect.err = SPN_ERR_INDEX_VERSION,
  },
  {
    .name = "unknown_format",
    .dir = "unknown_format",
    .expect.err = SPN_ERR_INDEX_FORMAT,
  },
  {
    .name = "no_format",
    .dir = "no_format",
    .expect = { .err = SPN_ERR_INDEX_MARKER_INVALID, .issue = SPN_ERR_CODEGEN_MISSING_KEY },
  },
  {
    .name = "path_leaves_the_index",
    .dir = "escapes",
    .expect = { .err = SPN_ERR_INDEX_MARKER_INVALID, .issue = SPN_ERR_CODEGEN_INVALID },
  },
  {
    .name = "version_zero",
    .dir = "version_zero",
    .expect = { .err = SPN_ERR_INDEX_MARKER_INVALID, .issue = SPN_ERR_CODEGEN_INVALID },
  },
};

sp_test_each(index_marker, load, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_t dir = { .sub = test_repo_path(mem, sp_test_format(t, "test/core/index/markers/{}", sp_fmt_cstr(it->dir))) };

  si_index_marker_t marker = sp_zero;
  spn_err_union_t err = sp_zero;
  sp_must_eq(t, it->expect.err, si_index_marker_load(mem, spn.intern, &spn.roots, dir, &marker, &err));
  sp_expect_eq(t, it->expect.err, err.kind);
  if (it->expect.issue) {
    sp_must_eq(t, 1, sp_da_size(err.manifest.issues));
    sp_expect_eq(t, it->expect.issue, err.manifest.issues[0].code);
  }
  if (it->expect.err) {
    return SP_OK;
  }

  sp_expect_eq(t, it->expect.version, marker.version);
  if (it->expect.version) {
    sp_expect_eq(t, it->expect.format, marker.format);
    sp_expect_str_eq_c(t, marker.path, it->expect.path);
  }
  return SP_OK;
}
