#include "index.h"

#include "index/marker.h"

typedef struct {
  const c8* name;
  spn_index_format_t format;
  const c8* path;
} test_t;

static const test_t tests [] = {
  {
    .name = "at_the_root",
    .format = SPN_INDEX_FORMAT_RELEASES,
    .path = "",
  },
  {
    .name = "under_a_path",
    .format = SPN_INDEX_FORMAT_MANIFESTS,
    .path = "A/B",
  },
};

sp_test_each(index_marker, save, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_t dir = { .sub = test_dir_str(t) };

  si_index_marker_t saved = {
    .version = si_index_marker_version,
    .format = it->format,
    .path = sp_cstr_as_str(it->path),
  };
  spn_err_union_t err = sp_zero;
  sp_must_eq(t, SPN_OK, si_index_marker_save(mem, &spn.roots, dir, saved, &err));

  si_index_marker_t loaded = sp_zero;
  sp_must_eq(t, SPN_OK, si_index_marker_load(mem, spn.intern, &spn.roots, dir, &loaded, &err));
  sp_expect_eq(t, saved.version, loaded.version);
  sp_expect_eq(t, saved.format, loaded.format);
  sp_expect_str_eq_c(t, loaded.path, it->path);
  return SP_OK;
}
