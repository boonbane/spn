#include "dag/dag_test.h"

#define reuse_test_max_blobs 4
#define reuse_test_age_s 600

typedef enum {
  OUTPUT_PATHLESS,
  OUTPUT_DECLARED,
  OUTPUT_TREE,
} output_t;

typedef struct {
  u32 runs;
} expect_t;

typedef struct {
  const c8* name;
  output_t output;
  bool evict;
  expect_t expect;
} test_t;

typedef struct {
  dag_test_env_t dag;
  output_t output;
} env_t;

static const test_t tests [] = {
  { .name = "hit_touches_pathless_output", .output = OUTPUT_PATHLESS, .expect = { .runs = 1 } },
  { .name = "hit_touches_settled_declared_output", .output = OUTPUT_DECLARED, .expect = { .runs = 1 } },
  { .name = "hit_touches_tree_entries", .output = OUTPUT_TREE, .expect = { .runs = 1 } },
  { .name = "evicted_blob_reruns", .output = OUTPUT_PATHLESS, .evict = true, .expect = { .runs = 2 } },
};

static spn_err_t execute_action(const si_dag_ctx_t* c, void* user_data) {
  env_t* env = (env_t*)user_data;
  env->dag.runs++;
  sp_path_t output = dag_test_at(&env->dag, si_dag_resolve_output(c, c->produces[0]));
  if (env->output == OUTPUT_TREE) {
    output = sp_path_join(env->dag.mem, output, sp_str_lit("A"));
  }
  return sp_fs_write_at(output, sp_str_lit("A"), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms) ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static sp_err_t run_once(sp_test_t* t, env_t* env) {
  dag_test_env_cold(&env->dag);
  spn_dag_t* g = dag_test_env_graph(&env->dag);
  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = dag_test_digest("I"),
    .execute = execute_action,
    .user_data = env,
  });
  spn_dag_id_t output = sp_zero;
  switch (env->output) {
    case OUTPUT_PATHLESS: output = spn_dag_add_output(g, sp_str_lit("O")); break;
    case OUTPUT_DECLARED: output = spn_dag_add_file(g, dag_test_env_rooted(&env->dag, sp_str_lit("O"))); break;
    case OUTPUT_TREE:     output = spn_dag_add_tree(g, dag_test_env_rooted(&env->dag, sp_str_lit("O"))); break;
  }
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, output));
  sp_must_eq(t, SPN_OK, dag_test_run(&env->dag));
  return SP_OK;
}

sp_test_each(dag_reuse, run, test_t, tests) {
  env_t env = { .output = it->output };
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) { .store = SPN_DAG_STORE_FILESYSTEM });
  sp_mem_t mem = env.dag.mem;
  spn_dag_layout_t layout = spn_dag_layout(mem, spn_path_from_root(SPN_PATH_ROOT_PROJECT));
  sp_path_t store = dag_test_at(&env.dag, layout.store);

  sp_try(run_once(t, &env));

  sp_path_t blobs [reuse_test_max_blobs] = sp_zero;
  u32 count = 0;
  sp_sys_dir_t handle = sp_zero;
  sp_must_ok(t, sp_fs_open_dir_at(store, &handle));
  SP_ALIGNED u8 buf [SP_SYS_DIR_MIN_BUF];
  sp_fs_dir_it_t dir = sp_fs_dir_it(handle);
  sp_fs_dir_entry_t entry = sp_zero;
  while (sp_fs_dir_it_next(&dir, sp_mem_slice(buf, sizeof(buf)), &entry)) {
    sp_must(t, count < reuse_test_max_blobs);
    blobs[count++] = sp_path_join(mem, store, entry.name);
  }
  sp_fs_dir_it_close(&dir);
  sp_fs_close_dir(handle);

  if (it->evict) {
    spn_dag_sweep(mem, &env.dag.roots, &layout, sp_zero_s(spn_dag_sweep_limits_t));
  }
  else {
    sp_sys_timespec_t used = { .tv_sec = (s64)(sp_tm_now_epoch().s - reuse_test_age_s) };
    sp_for(b, count) {
      sp_must_ok(t, sp_sys_set_times_s(blobs[b].dir, blobs[b].sub, used, used));
    }
  }

  sp_try(run_once(t, &env));
  sp_expect_eq(t, it->expect.runs, env.dag.runs);

  u64 now = sp_tm_now_epoch().s;
  sp_for(b, count) {
    sp_sys_file_meta_t meta = sp_zero;
    sp_must_ok(t, sp_fs_get_path_metadata_at(blobs[b], &meta));
    sp_expect(t, now - (u64)meta.mtime.tv_sec < spn_dag_touch_interval_s);
  }
  return SP_OK;
}
