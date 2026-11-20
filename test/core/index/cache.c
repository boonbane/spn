#include "index.h"

#include "index/cache.h"

typedef struct {
  const c8* name;
  struct {
    spn_index_format_t format;
    const c8* fixture;
    bool unplaced;
  } indexes [2];
  bool memoized;

  struct {
    bool exists;
    spn_err_t err;
    spn_semver_t version;
    u32 consulted;
  } expect;
} cache_test_t;

static const cache_test_t tests [] = {
  {
    .name = "first_wins_dir_first",
    .indexes = {
      { .format = SPN_INDEX_FORMAT_MANIFESTS, .fixture = "dir" },
      { .fixture = "jsonl" },
    },
    .expect = {
      .exists = true,
      .version = { 1, 5, 0 },
    },
  },
  {
    .name = "first_wins_jsonl_first",
    .indexes = {
      { .fixture = "jsonl" },
      { .format = SPN_INDEX_FORMAT_MANIFESTS, .fixture = "dir" },
    },
    .expect = {
      .exists = true,
      .version = { 1, 9, 0 },
    },
  },
  {
    .name = "fallthrough_when_absent",
    .indexes = {
      { .format = SPN_INDEX_FORMAT_MANIFESTS },
      { .format = SPN_INDEX_FORMAT_MANIFESTS, .fixture = "dir" },
    },
    .expect = {
      .exists = true,
      .version = { 1, 5, 0 },
    },
  },
  {
    .name = "error_stops_fallthrough",
    .indexes = {
      { .format = SPN_INDEX_FORMAT_MANIFESTS, .fixture = "broken" },
      { .fixture = "jsonl" },
    },
    .expect = {
      .err = SPN_ERR_MANIFEST_ISSUES,
    },
  },
  {
    .name = "unplaced_stops_the_lookup",
    .indexes = {
      { .unplaced = true },
      { .fixture = "jsonl" },
    },
    .expect = {
      .consulted = 1,
    },
  },
  {
    .name = "memoized",
    .indexes = {
      { .format = SPN_INDEX_FORMAT_MANIFESTS, .fixture = "dir" },
      { .fixture = "jsonl" },
    },
    .memoized = true,
    .expect = {
      .exists = true,
      .version = { 1, 5, 0 },
    },
  },
};

sp_test_each(index_cache, get_package, cache_test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  u32 slots = 0;
  sp_carr_detect_len(it->indexes, slots, it->indexes[slots].format || it->indexes[slots].fixture || it->indexes[slots].unplaced);

  si_da(si_index_place_t) indexes = sp_zero;
  sp_for(slot, slots) {
    if (it->indexes[slot].unplaced) {
      si_da_push(mem, indexes, sp_zero_s(si_index_place_t));
      continue;
    }

    sp_str_t location = sp_zero;
    if (it->indexes[slot].fixture) {
      location = test_repo_path(mem, sp_test_format(t, "test/core/index/indexes/{}", sp_fmt_cstr(it->indexes[slot].fixture)));
    } else {
      location = sp_fs_join_path(mem, test_dir_str(t), sp_test_format(t, "{}", sp_fmt_uint(slot)));
      sp_fs_create_dir(location);
    }

    si_da_push(mem, indexes, ((si_index_place_t) {
      .format = it->indexes[slot].format,
      .at = sp_path_from_str(location),
      .dir = location,
    }));
  }

  spn_index_cache_t cache = sp_zero;
  si_index_cache_init(&cache, mem, spn.intern, &spn.roots, indexes);

  sp_intern_str_t request = spn_intern_lit("A");

  spn_index_pkg_t* pkg = SP_NULLPTR;
  spn_err_union_t diag = sp_zero;
  spn_err_t err = spn_index_cache_get_package(&cache, request, &pkg, &diag);

  sp_expect_eq(t, it->expect.err, err);
  sp_must_eq(t, it->expect.exists, pkg != SP_NULLPTR);
  if (it->expect.consulted) {
    sp_expect_eq(t, it->expect.consulted, cache.consulted);
  }

  if (pkg) {
    sp_must_eq(t, 1, sp_da_size(pkg->releases));
    sp_expect(t, spn_semver_eq(it->expect.version, pkg->releases[0].version));
  }

  if (it->memoized) {
    spn_index_pkg_t* again = SP_NULLPTR;
    spn_index_cache_get_package(&cache, request, &again, &diag);
    sp_must(t, again == pkg);
  }
  return SP_OK;
}
