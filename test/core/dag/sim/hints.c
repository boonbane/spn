#include "dag/dag_test.h"

#define HINTS_TEST_MAX_FILES 4

typedef struct {
  const c8* path;
  const c8* content;
  spn_path_root_t root;
} file_t;

typedef struct {
  const c8* name;
  file_t files [HINTS_TEST_MAX_FILES];
} test_t;

static const test_t tests [] = {
  {
    .name = "rooted_file",
    .files = { { .path = "A", .content = "a", .root = SPN_PATH_ROOT_PROJECT } }
  },
  {
    .name = "nested_sub",
    .files = { { .path = "D/E/A", .content = "a", .root = SPN_PATH_ROOT_PROJECT } }
  },
  {
    .name = "outside_roots_absolute",
    .files = { { .path = "A", .content = "a" } }
  },
  {
    .name = "mixed_roots",
    .files = {
      { .path = "A", .content = "a", .root = SPN_PATH_ROOT_PROJECT },
      { .path = "D/B", .content = "bb", .root = SPN_PATH_ROOT_PROJECT },
      { .path = "C", .content = "ccc" },
    }
  },
};

static spn_path_t make_key(dag_test_env_t* env, const file_t* file) {
  spn_path_t rooted = dag_test_env_rooted(env, sp_str_view(file->path));
  if (file->root == SPN_PATH_ROOT_NONE) {
    return (spn_path_t) { .sub = dag_test_render(env, rooted) };
  }
  return rooted;
}

sp_test_each(dag_hints, roundtrip, test_t, tests) {
  dag_test_env_t env;
  dag_test_env_init(&env, t, (dag_test_env_config_t) sp_zero);
  sp_str_t path = dag_test_env_path(&env, sp_str_lit("files"));

  u32 count = 0;
  sp_carr_detect_len(it->files, count, it->files[count].path);
  sp_for(f, count) {
    dag_test_env_create(&env, sp_str_view(it->files[f].path), sp_str_view(it->files[f].content));
  }

  spn_dag_digest_t hashed [HINTS_TEST_MAX_FILES] = sp_zero;
  sp_for(f, count) {
    sp_must_eq(t, SPN_OK, spn_dag_file_cache_digest(&env.files, make_key(&env, &it->files[f]), &hashed[f]));
  }
  spn_dag_file_cache_flush(&env.files, path);

  sp_str_t content = sp_zero;
  sp_must_eq(t, SP_OK, sp_io_read_file(env.mem, path, &content));
  sp_expect(t, sp_str_starts_with(content, sp_str_lit("3\n")));
  sp_for(f, count) {
    spn_path_t key = make_key(&env, &it->files[f]);
    sp_str_t row = sp_fmt(env.mem, " {} {}\n", sp_fmt_uint(key.root), sp_fmt_str(key.sub)).value;
    sp_expect(t, sp_str_contains(content, row));
  }

  spn_dag_stats_t stats = sp_zero;
  spn_dag_file_cache_t reloaded = sp_zero;
  spn_dag_file_cache_init(&reloaded, env.mem, &env.roots);
  reloaded.stats = &stats;
  spn_dag_file_cache_load(&reloaded, path);
  sp_for(f, count) {
    spn_dag_digest_t digest = sp_zero;
    sp_must_eq(t, SPN_OK, spn_dag_file_cache_digest(&reloaded, make_key(&env, &it->files[f]), &digest));
    sp_expect(t, spn_dag_digest_equal(digest, hashed[f]));
  }
  sp_expect_eq(t, 0u, sp_atomic_u32_load(&stats.hashed_files, SP_ATOMIC_SEQ_CST));

  return SP_OK;
}

typedef struct {
  dag_test_env_t dag;
  spn_path_t obs;
} env_t;

typedef struct {
  bool cold;
  bool create;
} step_t;

static const step_t steps [] = {
  { .create = true },
  { .cold = true, .create = true },
  { .cold = true },
};

static spn_err_t execute_action(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* dag_env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  env_t* env = (env_t*)user_data;
  spn_try(dag_test_exec_stamp(g, action, &env->dag, dag_env, outputs, obs));
  spn_dag_observe(obs, (spn_dag_obs_t) {
    .kind = SPN_DAG_OBS_FILE,
    .path = env->obs
  });
  return SPN_OK;
}

static sp_err_t execute_observing(sp_test_t* t, env_t* env, const c8* identity) {
  spn_dag_file_cache_invalidate_all(&env->dag.files);
  spn_dag_t* g = dag_test_env_graph(&env->dag);
  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .kind = SPN_DAG_ACTION_DISCOVERED,
    .identity = dag_test_digest(identity),
    .execute = execute_action,
    .user_data = env
  });
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env->dag, sp_str_lit("O")))));
  sp_expect_eq(t, SPN_OK, spn_dag_execute(g, action, &env->dag.env));
  return SP_OK;
}

sp_test(dag_hints, refreshed_on_hit) {
  env_t env = sp_zero;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) {
    .store = SPN_DAG_STORE_MEM,
    .discovery = true
  });
  env.obs = dag_test_env_rooted(&env.dag, sp_str_lit("H"));

  sp_carr_for(steps, s) {
    if (steps[s].cold) {
      dag_test_env_cold(&env.dag);
    }
    if (steps[s].create) {
      dag_test_env_create(&env.dag, sp_str_lit("H"), sp_str_lit("A"));
    }
    sp_err_t err = execute_observing(t, &env, "refresh");
    if (err) {
      return err;
    }
    sp_expect_eq(t, 1u, env.dag.runs);
  }

  sp_str_t hints = sp_zero;
  sp_must_eq(t, SP_OK, sp_io_read_file(env.dag.mem, dag_test_env_path(&env.dag, sp_str_lit("files")), &hints));
  sp_sys_file_meta_t sys = sp_zero;
  sp_must_eq(t, SPN_OK, spn_dag_file_cache_stat(&env.dag.files, env.obs, &sys));
  sp_str_t mtime = sp_fmt(env.dag.mem, " {} {} ", sp_fmt_int((s64)sys.mtime.tv_sec), sp_fmt_int((s64)sys.mtime.tv_nsec)).value;
  sp_expect(t, sp_str_contains(hints, mtime));
  return SP_OK;
}

sp_test(dag_hints, pinned_obs_not_recorded) {
  env_t env = sp_zero;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) {
    .checkout = "checkout",
    .store = SPN_DAG_STORE_MEM,
    .discovery = true,
    .pinned = 1u << SPN_PATH_ROOT_CHECKOUT
  });
  env.obs = (spn_path_t) { .root = SPN_PATH_ROOT_CHECKOUT, .sub = sp_str_lit("locked.h") };
  dag_test_env_create(&env.dag, sp_str_lit("checkout/locked.h"), sp_str_lit("A"));

  sp_err_t err = execute_observing(t, &env, "pinned");
  if (err) {
    return err;
  }

  sp_str_t path = dag_test_env_path(&env.dag, sp_str_lit("files"));
  spn_dag_file_cache_flush(&env.dag.files, path);
  sp_str_t hints = sp_zero;
  sp_must_eq(t, SP_OK, sp_io_read_file(env.dag.mem, path, &hints));
  sp_expect(t, sp_str_contains(hints, sp_fmt(env.dag.mem, " {} O", sp_fmt_uint(SPN_PATH_ROOT_PROJECT)).value));
  sp_expect(t, !sp_str_contains(hints, sp_str_lit("locked.h")));
  return SP_OK;
}
