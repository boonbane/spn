#include "index.h"

#include "git/key.h"
#include "paths/paths.h"

typedef struct {
  spn_path_root_t root;
  const c8* sub;
  bool keyed;
} location_expect_t;

typedef struct {
  const c8* name;
  spn_index_protocol_t protocol;
  const c8* url;
  const c8* dir;
  spn_path_root_t root;
  const c8* root_sub;
  location_expect_t expect;
} location_test_t;

static const location_test_t location_tests [] = {
  {
    .name = "git_is_keyed_under_the_root",
    .protocol = SPN_INDEX_PROTOCOL_GIT,
    .url = "U",
    .root = SPN_PATH_ROOT_INDEX,
    .expect = { .root = SPN_PATH_ROOT_INDEX, .keyed = true },
  },
  {
    .name = "http_is_keyed_under_the_root",
    .protocol = SPN_INDEX_PROTOCOL_HTTP,
    .url = "U",
    .root = SPN_PATH_ROOT_INDEX,
    .expect = { .root = SPN_PATH_ROOT_INDEX, .keyed = true },
  },
  {
    .name = "root_sub_is_kept",
    .protocol = SPN_INDEX_PROTOCOL_GIT,
    .url = "U",
    .root = SPN_PATH_ROOT_INDEX,
    .root_sub = "S",
    .expect = { .root = SPN_PATH_ROOT_INDEX, .sub = "S", .keyed = true },
  },
  {
    .name = "dir_is_the_dir_itself",
    .protocol = SPN_INDEX_PROTOCOL_DIR,
    .dir = "/D",
    .root = SPN_PATH_ROOT_INDEX,
    .expect = { .sub = "/D" },
  },
};

sp_test_each(index_location, resolve, location_test_t, location_tests) {
  sp_mem_t mem = sp_test_arena(t);
  sp_str_t url = sp_cstr_as_str(it->url ? it->url : "");
  spn_index_info_t index = { .protocol = it->protocol };
  switch (it->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      index.git.url = url;
      break;
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      index.http.url = url;
      break;
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      index.dir.path = (spn_path_t) { .sub = sp_cstr_as_str(it->dir) };
      break;
    }
  }

  spn_path_t root = { .root = it->root, .sub = sp_cstr_as_str(it->root_sub ? it->root_sub : "") };
  spn_path_t location = spn_index_location(&index, mem, root);

  sp_str_t sub = sp_cstr_as_str(it->expect.sub ? it->expect.sub : "");
  if (it->expect.keyed) {
    sub = sp_fs_join_path(mem, sub, spn_git_db_key(mem, url));
  }
  sp_expect_eq(t, location.root, it->expect.root);
  sp_expect_str_eq(t, location.sub, sub);
  return SP_OK;
}
