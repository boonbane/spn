#include "dag/dag_test.h"

#define sweep_test_max_items 4
#define sweep_test_block 4096

typedef enum {
  ITEM_NONE,
  ITEM_BLOB,
  ITEM_STRONG,
  ITEM_WEAK,
} item_kind_t;

typedef struct {
  bool evicted;
} expect_t;

typedef struct {
  item_kind_t kind;
  const c8* id;
  u64 age_s;
  expect_t expect;
} item_t;

typedef struct {
  const c8* name;
  item_t items [sweep_test_max_items];
  spn_dag_sweep_limits_t limits;
} test_t;

static const test_t tests [] = {
  {
    .name = "at_max_is_untouched",
    .items = {
      { .kind = ITEM_BLOB, .id = "A", .age_s = 300 },
      { .kind = ITEM_BLOB, .id = "B", .age_s = 200 },
    },
    .limits = { .max_bytes = 2 * sweep_test_block },
  },
  {
    .name = "over_max_evicts_oldest_first",
    .items = {
      { .kind = ITEM_BLOB, .id = "A", .age_s = 200 },
      { .kind = ITEM_BLOB, .id = "B", .age_s = 300, .expect = { .evicted = true } },
      { .kind = ITEM_BLOB, .id = "C", .age_s = 100 },
    },
    .limits = { .max_bytes = 2 * sweep_test_block, .keep_bytes = 2 * sweep_test_block },
  },
  {
    .name = "evicts_down_to_keep",
    .items = {
      { .kind = ITEM_BLOB, .id = "A", .age_s = 400, .expect = { .evicted = true } },
      { .kind = ITEM_BLOB, .id = "B", .age_s = 300, .expect = { .evicted = true } },
      { .kind = ITEM_BLOB, .id = "C", .age_s = 200 },
      { .kind = ITEM_BLOB, .id = "D", .age_s = 100 },
    },
    .limits = { .max_bytes = 3 * sweep_test_block, .keep_bytes = 2 * sweep_test_block },
  },
  {
    .name = "min_age_protects_recent",
    .items = {
      { .kind = ITEM_BLOB, .id = "A", .age_s = 300, .expect = { .evicted = true } },
      { .kind = ITEM_BLOB, .id = "B", .age_s = 30 },
    },
    .limits = { .min_age_s = 60 },
  },
  {
    .name = "entries_share_one_order_with_blobs",
    .items = {
      { .kind = ITEM_WEAK, .id = "A", .age_s = 300, .expect = { .evicted = true } },
      { .kind = ITEM_BLOB, .id = "B", .age_s = 100 },
      { .kind = ITEM_STRONG, .id = "C", .age_s = 200, .expect = { .evicted = true } },
    },
    .limits = { .max_bytes = sweep_test_block, .keep_bytes = sweep_test_block },
  },
};

sp_test_each(dag_sweep, evict, test_t, tests) {
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

  u32 count = 0;
  sp_carr_detect_len(it->items, count, it->items[count].kind);

  sp_tm_epoch_t now = sp_tm_now_epoch();
  sp_path_t paths [sweep_test_max_items] = sp_zero;
  u32 evicted = 0;
  sp_for(i, count) {
    const item_t* item = &it->items[i];
    sp_str_t id = sp_cstr_as_str(item->id);
    spn_dag_digest_t key = dag_test_digest(item->id);
    sp_str_t hex = spn_dag_digest_hex(mem, key);

    spn_path_t path = sp_zero;
    switch (item->kind) {
      case ITEM_NONE: {
        break;
      }
      case ITEM_BLOB: {
        spn_dag_digest_t digest = sp_zero;
        sp_must_eq(t, SPN_OK, spn_dag_store_put(&store, id.data, id.len, sp_str_lit("blob"), &digest));
        path = spn_path_join(mem, layout.store, hex);
        break;
      }
      case ITEM_STRONG: {
        spn_dag_action_cache_put(&strong, key, SP_NULLPTR, 0);
        path = spn_path_join(mem, layout.strong, sp_fmt(mem, "{}.txt", sp_fmt_str(hex)).value);
        break;
      }
      case ITEM_WEAK: {
        spn_dag_obs_set_t set = { .table = &weak };
        spn_dag_obs_set_put(&set, key);
        path = spn_path_join(mem, layout.weak, sp_fmt(mem, "{}.txt", sp_fmt_str(hex)).value);
        break;
      }
    }
    paths[i] = spn_path_at(&roots, path);
    sp_sys_timespec_t used = { .tv_sec = (s64)(now.s - item->age_s) };
    sp_must_ok(t, sp_sys_set_times_s(paths[i].dir, paths[i].sub, used, used));

    evicted += item->expect.evicted;
  }

  spn_dag_sweep_t sweep = spn_dag_sweep(mem, &roots, &layout, it->limits);
  sp_expect_eq(t, sweep.bytes, (u64)count * sweep_test_block);
  sp_expect_eq(t, sweep.freed, (u64)evicted * sweep_test_block);
  sp_expect_eq(t, sweep.removed, evicted);
  sp_for(i, count) {
    sp_test_kv_c(t, "item", it->items[i].id);
    sp_sys_file_meta_t meta = sp_zero;
    sp_expect_err_eq(t, sp_fs_get_link_metadata_at(paths[i], &meta), it->items[i].expect.evicted ? SP_ERR_SYS_NOT_FOUND : SP_OK);
  }

  spn_path_roots_close(&roots);
  return SP_OK;
}

typedef struct {
  const c8* name;
  u64 min_age_s;
  u64 expect;
} min_age_test_t;

static const min_age_test_t min_age_tests [] = {
  { .name = "zero_is_the_touch_interval", .min_age_s = 0, .expect = spn_dag_touch_interval_s },
  { .name = "adds_the_touch_interval", .min_age_s = 3600, .expect = 3600 + spn_dag_touch_interval_s },
  { .name = "saturates_at_max", .min_age_s = SP_LIMIT_U64_MAX, .expect = SP_LIMIT_U64_MAX },
};

sp_test_each(dag_sweep, min_age, min_age_test_t, min_age_tests) {
  sp_expect_eq(t, spn_dag_sweep_min_age(it->min_age_s), it->expect);
  return SP_OK;
}
