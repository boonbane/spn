#include "git.h"

#define PATCH_TEST_MAX_FILES 4

typedef struct {
  bool hash_matches_pair;
} expect_t;

typedef struct {
  const c8* name;
  const c8* files [PATCH_TEST_MAX_FILES];
  const c8* pair [PATCH_TEST_MAX_FILES];
  expect_t expect;
} test_t;

static const test_t tests [] = {
  // same contents at different paths -> same hash: content keys identity, not paths
  {
    .name = "hash_ignores_paths",
    .files = { "A", "B" },
    .pair = { "A", "B" },
    .expect = { .hash_matches_pair = true },
  },
  // different content -> different hash
  {
    .name = "hash_tracks_content",
    .files = { "A" },
    .pair = { "B" },
  },
  // same contents, reversed order -> different hash: order is part of identity
  {
    .name = "hash_tracks_order",
    .files = { "A", "B" },
    .pair = { "B", "A" },
  },
};

static sp_err_t hash_patch_files(sp_test_t* t, u32 set, const c8* const* contents, u32 count, sp_hash_t* hash) {
  sp_mem_t mem = sp_test_arena(t);
  si_da(sp_str_t) files = SP_NULLPTR;
  sp_for(at, count) {
    sp_str_t path = sp_fs_join_path(mem, test_dir_str(t), sp_fmt(mem, "{}.{}.patch", sp_fmt_uint(set), sp_fmt_uint(at)).value);
    sp_must_ok(t, sp_fs_write(path, sp_cstr_as_str(contents[at]), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms));
    si_da_push(mem, files, path);
  }

  u32 missing = 0;
  *hash = spn_git_patch_hash(files, &missing);
  sp_must_ne(t, *hash, (sp_hash_t)0);
  return SP_OK;
}

sp_test_each(git_patch, set_hash, test_t, tests) {
  u32 files = 0;
  sp_carr_detect_len(it->files, files, it->files[files]);
  u32 pair = 0;
  sp_carr_detect_len(it->pair, pair, it->pair[pair]);

  sp_hash_t hash = 0;
  sp_hash_t other = 0;
  sp_try(hash_patch_files(t, 0, it->files, files, &hash));
  sp_try(hash_patch_files(t, 1, it->pair, pair, &other));
  sp_expect_eq(t, hash == other, it->expect.hash_matches_pair);
  return SP_OK;
}

// a missing file fails the load and reports which file
sp_test(git_patch, missing_file) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t present = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("A"));
  sp_must_ok(t, sp_fs_write(present, sp_str_lit("A"), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms));
  si_da(sp_str_t) files = SP_NULLPTR;
  si_da_push(mem, files, present);
  si_da_push(mem, files, sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("B")));

  u32 missing = 0;
  sp_expect_eq(t, spn_git_patch_hash(files, &missing), (sp_hash_t)0);
  sp_expect_eq(t, missing, 1u);
  return SP_OK;
}
