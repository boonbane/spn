#include "dag/dag_test.h"

typedef struct {
  const c8* path;
  const c8* content;
  spn_dag_obs_kind_t kind;
  const c8* filter;
} obs_spec_t;

typedef struct {
  const c8* path;
  const c8* content;
} plant_t;

typedef struct {
  obs_spec_t obs [DAG_TEST_MAX_INPUTS];
  plant_t plants [2];
  const c8* removed [2];
  u32 expect_runs;
} run_t;

typedef struct {
  const c8* name;
  const c8* pinned;
  spn_path_root_t root;
  run_t runs [DAG_TEST_MAX_OPS];
} test_t;

typedef struct {
  dag_test_env_t dag;
  spn_path_root_t root;
  const run_t* run;
} env_t;

static const test_t tests [] = {
  {
    .name = "pinned_change_hits",
    .pinned = "P",
    .root = SPN_PATH_ROOT_STORAGE,
    .runs = {
      { .obs = { { "P/H", "A" } }, .expect_runs = 1 },
      { .obs = { { "P/H", "B" } }, .expect_runs = 1 },
    }
  },
  {
    .name = "pinned_removed_hits",
    .pinned = "P",
    .root = SPN_PATH_ROOT_STORAGE,
    .runs = {
      { .obs = { { "P/H", "A" } }, .expect_runs = 1 },
      { .removed = { "P/H" }, .expect_runs = 1 },
    }
  },
  {
    .name = "unpinned_change_reruns",
    .root = SPN_PATH_ROOT_STORAGE,
    .runs = {
      { .obs = { { "P/H", "A" } }, .expect_runs = 1 },
      { .obs = { { "P/H", "B" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "outside_the_pinned_dir_reruns",
    .pinned = "P",
    .root = SPN_PATH_ROOT_STORAGE,
    .runs = {
      { .obs = { { "Q/H", "A" } }, .expect_runs = 1 },
      { .obs = { { "Q/H", "B" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "pinned_dir_requires_component_boundary",
    .pinned = "P",
    .root = SPN_PATH_ROOT_STORAGE,
    .runs = {
      { .obs = { { "PX/H", "A" } }, .expect_runs = 1 },
      { .obs = { { "PX/H", "B" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "same_sub_under_another_root_reruns",
    .pinned = "P",
    .root = SPN_PATH_ROOT_PROJECT,
    .runs = {
      { .obs = { { "P/H", "A" } }, .expect_runs = 1 },
      { .obs = { { "P/H", "B" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "pinned_enumeration_hits",
    .pinned = "P",
    .root = SPN_PATH_ROOT_STORAGE,
    .runs = {
      { .obs = { { .path = "P/inc", .kind = SPN_DAG_OBS_ENUMERATION, .filter = "*.h" } }, .plants = { { "P/inc/a.h", "X" } }, .expect_runs = 1 },
      { .obs = { { .path = "P/inc", .kind = SPN_DAG_OBS_ENUMERATION, .filter = "*.h" } }, .plants = { { "P/inc/b.h", "X" } }, .expect_runs = 1 },
    }
  },
  {
    .name = "unpinned_enumeration_reruns",
    .root = SPN_PATH_ROOT_STORAGE,
    .runs = {
      { .obs = { { .path = "P/inc", .kind = SPN_DAG_OBS_ENUMERATION, .filter = "*.h" } }, .plants = { { "P/inc/a.h", "X" } }, .expect_runs = 1 },
      { .obs = { { .path = "P/inc", .kind = SPN_DAG_OBS_ENUMERATION, .filter = "*.h" } }, .plants = { { "P/inc/b.h", "X" } }, .expect_runs = 2 },
    }
  },
};

static sp_path_t root_path(env_t* env, sp_str_t rel) {
  return spn_path_at(&env->dag.roots, (spn_path_t) { .root = env->root, .sub = rel });
}

static spn_err_t execute_action(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* dag_env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  env_t* env = (env_t*)user_data;
  spn_try(dag_test_exec_stamp(g, action, &env->dag, dag_env, outputs, obs));
  sp_carr_for(env->run->obs, it) {
    const obs_spec_t* spec = &env->run->obs[it];
    if (!spec->path) {
      break;
    }
    spn_dag_observe(obs, (spn_dag_obs_t) {
      .kind = spec->kind,
      .path = { .root = env->root, .sub = sp_str_view(spec->path) },
      .filter = spec->filter ? sp_str_view(spec->filter) : sp_str_lit("")
    });
  }
  return SPN_OK;
}

static void prepare_run(env_t* env, const run_t* run) {
  sp_carr_for(run->obs, it) {
    if (!run->obs[it].path) {
      break;
    }
    if (run->obs[it].content) {
      dag_test_create(root_path(env, sp_str_view(run->obs[it].path)), sp_str_view(run->obs[it].content));
    }
  }
  sp_carr_for(run->plants, it) {
    if (!run->plants[it].path) {
      break;
    }
    dag_test_create(root_path(env, sp_str_view(run->plants[it].path)), sp_str_view(run->plants[it].content));
  }
  sp_carr_for(run->removed, it) {
    if (!run->removed[it]) {
      break;
    }
    sp_fs_remove_file_at(root_path(env, sp_str_view(run->removed[it])));
  }
}

sp_test_each(dag_pinned, runs, test_t, tests) {
  env_t env = sp_zero;
  env.root = it->root;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) {
    .storage = "storage",
    .store = SPN_DAG_STORE_MEM,
    .discovery = true,
    .pinned = it->pinned
  });

  sp_carr_for(it->runs, r) {
    const run_t* run = &it->runs[r];
    if (!run->expect_runs) {
      break;
    }

    sp_test_kv(t, "run", sp_fmt(env.dag.mem, "{}", sp_fmt_uint(r)).value);
    env.run = run;
    spn_dag_file_cache_invalidate_all(&env.dag.files);
    prepare_run(&env, run);

    spn_dag_t* g = dag_test_env_graph(&env.dag);
    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .kind = SPN_DAG_ACTION_DISCOVERED,
      .identity = dag_test_digest(it->name),
      .execute = execute_action,
      .user_data = &env
    });
    spn_dag_id_t obj = spn_dag_add_file(g, dag_test_env_rooted(&env.dag, sp_str_lit("O")));
    sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, obj));

    sp_expect_eq(t, SPN_OK, spn_dag_execute(g, action, &env.dag.env));
    sp_expect_eq(t, run->expect_runs, env.dag.runs);
  }

  return SP_OK;
}

typedef struct {
  const c8* name;
  const c8* pinned;
  u32 expect_runs [2];
} source_test_t;

static const source_test_t source_tests [] = {
  {
    .name = "pinned_source_change_hits",
    .pinned = "P",
    .expect_runs = { 1, 1 }
  },
  {
    .name = "unpinned_source_change_reruns",
    .expect_runs = { 1, 2 }
  },
};

sp_test_each(dag_pinned, source, source_test_t, source_tests) {
  env_t env = sp_zero;
  env.root = SPN_PATH_ROOT_STORAGE;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) {
    .storage = "storage",
    .store = SPN_DAG_STORE_MEM,
    .pinned = it->pinned
  });

  const c8* contents [2] = { "A", "B" };
  sp_carr_for(it->expect_runs, r) {
    sp_test_kv(t, "run", sp_fmt(env.dag.mem, "{}", sp_fmt_uint(r)).value);
    dag_test_create(root_path(&env, sp_str_lit("P/S.c")), sp_str_view(contents[r]));
    spn_dag_file_cache_invalidate_all(&env.dag.files);

    spn_dag_t* g = dag_test_env_graph(&env.dag);
    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .identity = dag_test_digest(it->name),
      .execute = dag_test_exec_stamp,
      .user_data = &env.dag
    });
    spn_dag_action_add_input(g, action, spn_dag_add_file(g, (spn_path_t) { .root = SPN_PATH_ROOT_STORAGE, .sub = sp_str_lit("P/S.c") }));
    spn_dag_id_t obj = spn_dag_add_file(g, dag_test_env_rooted(&env.dag, sp_str_lit("O")));
    sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, obj));

    sp_expect_eq(t, SPN_OK, spn_dag_run(g, &env.dag.env));
    sp_expect_eq(t, it->expect_runs[r], env.dag.runs);
  }

  return SP_OK;
}

sp_test(dag_pinned, pinned_dirs_change_weak_key) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t storage = sp_zero;
  const spn_path_roots_t* roots = paths_test_roots_build((paths_test_roots_t) { .project = "/R" }, &storage);
  storage.pinned = sp_da_new(mem, spn_path_t);

  const c8* dirs [2] = { "P", "Q" };
  spn_dag_digest_t keys [3] = sp_zero;
  sp_carr_for(keys, it) {
    spn_dag_t* g = spn_dag_new(mem, roots);
    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .identity = dag_test_digest("cc"),
      .execute = dag_test_exec_noop
    });
    keys[it] = spn_dag_weak_key(g, action);
    if (it < sp_carr_len(dirs)) {
      sp_da_push(storage.pinned, ((spn_path_t) { .root = SPN_PATH_ROOT_STORAGE, .sub = sp_str_view(dirs[it]) }));
    }
  }

  sp_expect(t, !spn_dag_digest_equal(keys[0], keys[1]));
  sp_expect(t, !spn_dag_digest_equal(keys[1], keys[2]));
  return SP_OK;
}

sp_test(dag_pinned, pinned_output_rejected) {
  sp_mem_t mem = sp_test_arena(t);
  spn_path_roots_t storage = sp_zero;
  const spn_path_roots_t* roots = paths_test_roots_build((paths_test_roots_t) { .project = "/R", .storage = "/C" }, &storage);
  storage.pinned = sp_da_new(mem, spn_path_t);
  sp_da_push(storage.pinned, ((spn_path_t) { .root = SPN_PATH_ROOT_STORAGE, .sub = sp_str_lit("P") }));

  spn_dag_t* g = spn_dag_new(mem, roots);
  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) { .execute = dag_test_exec_noop });
  spn_dag_id_t inside = spn_dag_add_file(g, (spn_path_t) { .root = SPN_PATH_ROOT_STORAGE, .sub = sp_str_lit("P/O") });
  spn_dag_id_t beside = spn_dag_add_file(g, (spn_path_t) { .root = SPN_PATH_ROOT_STORAGE, .sub = sp_str_lit("B") });
  spn_dag_id_t outside = spn_dag_add_file(g, (spn_path_t) { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_str_lit("P/O") });

  sp_expect_eq(t, SPN_ERR_DAG_PINNED_OUTPUT, spn_dag_action_add_output(g, action, inside));
  sp_expect_eq(t, SPN_OK, spn_dag_action_add_output(g, action, beside));
  sp_expect_eq(t, SPN_OK, spn_dag_action_add_output(g, action, outside));
  return SP_OK;
}
