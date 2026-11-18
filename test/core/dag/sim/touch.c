#include "dag/dag_test.h"

typedef enum {
  USE_STRONG_GET,
  USE_WEAK_GET,
  USE_STORE_TOUCH,
  USE_STORE_PUT,
  USE_STORE_PUT_FILE,
} use_t;

typedef struct {
  bool refreshed;
} expect_t;

typedef struct {
  const c8* name;
  use_t use;
  u64 age_s;
  expect_t expect;
} test_t;

static const test_t tests [] = {
  { .name = "strong_entry_load", .use = USE_STRONG_GET, .age_s = 600, .expect = { .refreshed = true } },
  { .name = "weak_entry_load", .use = USE_WEAK_GET, .age_s = 600, .expect = { .refreshed = true } },
  { .name = "blob_touch", .use = USE_STORE_TOUCH, .age_s = 600, .expect = { .refreshed = true } },
  { .name = "blob_put_again", .use = USE_STORE_PUT, .age_s = 600, .expect = { .refreshed = true } },
  { .name = "blob_put_file_again", .use = USE_STORE_PUT_FILE, .age_s = 600, .expect = { .refreshed = true } },
  { .name = "recently_used_is_left_alone", .use = USE_STORE_TOUCH, .age_s = 60 },
};

sp_test_each(dag_touch, use, test_t, tests) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t roots = sp_zero;
  spn_path_roots_set(&roots, mem, SPN_PATH_ROOT_PROJECT, sp_test_dir(t));
  spn_dag_layout_t layout = spn_dag_layout(mem, spn_path_from_root(SPN_PATH_ROOT_PROJECT));

  spn_dag_store_t store = sp_zero;
  spn_dag_store_init(&store, (spn_dag_store_config_t) {
    .kind = SPN_DAG_STORE_FILESYSTEM,
    .mem = mem,
    .roots = &roots,
    .dir = layout.store,
  });
  spn_dag_action_cache_t strong = sp_zero;
  spn_dag_action_cache_init(&strong, mem, &roots, layout.strong);
  spn_dag_obs_table_t weak = sp_zero;
  spn_dag_obs_table_init(&weak, mem, &roots, layout.weak);

  sp_str_t blob = sp_str_lit("A");
  spn_dag_digest_t key = spn_dag_digest(blob.data, blob.len);
  sp_str_t hex = spn_dag_digest_hex(mem, key);
  sp_path_t source = sp_path_join(mem, sp_test_dir(t), sp_str_lit("A"));
  spn_dag_digest_t digest = sp_zero;

  spn_path_t path = sp_zero;
  switch (it->use) {
    case USE_STRONG_GET: {
      spn_dag_action_cache_put(&strong, key, SP_NULLPTR, 0);
      path = spn_path_join(mem, layout.strong, sp_fmt(mem, "{}.txt", sp_fmt_str(hex)).value);
      break;
    }
    case USE_WEAK_GET: {
      spn_dag_obs_set_t set = { .table = &weak };
      spn_dag_obs_set_put(&set, key);
      path = spn_path_join(mem, layout.weak, sp_fmt(mem, "{}.txt", sp_fmt_str(hex)).value);
      break;
    }
    case USE_STORE_TOUCH:
    case USE_STORE_PUT: {
      sp_must_eq(t, SPN_OK, spn_dag_store_put(&store, blob.data, blob.len, sp_str_lit("blob"), &digest));
      path = spn_path_join(mem, layout.store, hex);
      break;
    }
    case USE_STORE_PUT_FILE: {
      dag_test_create(source, blob);
      sp_must_eq(t, SPN_OK, spn_dag_store_put_file(&store, source, sp_str_lit("blob"), &digest));
      path = spn_path_join(mem, layout.store, hex);
      break;
    }
  }

  sp_path_t at = spn_path_at(&roots, path);
  sp_sys_timespec_t used = { .tv_sec = (s64)(sp_tm_now_epoch().s - it->age_s) };
  sp_must_ok(t, sp_sys_set_times_s(at.dir, at.sub, used, used));

  switch (it->use) {
    case USE_STRONG_GET: {
      spn_dag_action_entry_t entry = sp_zero;
      spn_dag_action_cache_init(&strong, mem, &roots, layout.strong);
      sp_must(t, spn_dag_action_cache_get(&strong, key, &entry));
      break;
    }
    case USE_WEAK_GET: {
      spn_dag_pathset_t set = sp_zero;
      spn_dag_obs_table_init(&weak, mem, &roots, layout.weak);
      sp_must(t, spn_dag_obs_table_get(&weak, key, &set));
      break;
    }
    case USE_STORE_TOUCH: {
      spn_dag_store_touch(&store, key);
      break;
    }
    case USE_STORE_PUT: {
      sp_must_eq(t, SPN_OK, spn_dag_store_put(&store, blob.data, blob.len, sp_str_lit("blob"), &digest));
      break;
    }
    case USE_STORE_PUT_FILE: {
      sp_must_eq(t, SPN_OK, spn_dag_store_put_file(&store, source, sp_str_lit("blob"), &digest));
      break;
    }
  }

  sp_sys_file_meta_t meta = sp_zero;
  sp_must_ok(t, sp_fs_get_path_metadata_at(at, &meta));
  sp_expect_eq(t, it->expect.refreshed, meta.mtime.tv_sec != used.tv_sec);

  spn_path_roots_close(&roots);
  return SP_OK;
}
