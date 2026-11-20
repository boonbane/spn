#include "index.h"

typedef struct {
  const c8* name;
  spn_index_protocol_t protocol;
} test_t;

static const test_t tests [] = {
  {
    .name = "http",
    .protocol = SPN_INDEX_PROTOCOL_HTTP,
  },
  {
    .name = "dir",
    .protocol = SPN_INDEX_PROTOCOL_DIR,
  },
};

sp_test_each(index_publish, refused, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  sp_str_t location = sp_fs_join_path(mem, test_dir_str(t), sp_str_lit("index"));
  sp_fs_create_dir(location);

  spn_index_info_t index = {
    .location = { .at = sp_path_from_str(location), .dir = location },
    .protocol = it->protocol,
  };

  spn_index_release_t rel = {
    .name = spn_intern_lit("A"),
    .version = { 1, 0, 0 },
  };

  sp_atomic_s32_t cancelled = sp_zero;
  sp_expect_eq(t, SPN_ERR_INDEX_PUBLISH_PROTOCOL, si_index_publish(&index, &rel, &cancelled));

  return SP_OK;
}
