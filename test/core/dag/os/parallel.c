#include "dag/dag_test.h"
#include "thread_pool/thread_pool.h"

typedef struct {
  const c8* path;
  const c8* content;
} source_t;

typedef struct {
  const c8* identity;
  const c8* inputs [DAG_TEST_MAX_INPUTS];
  const c8* discovers [DAG_TEST_MAX_INPUTS];
  const c8* output;
  bool tree;
  bool fails;
} action_t;

typedef struct {
  source_t sources [DAG_TEST_MAX_INPUTS];
  const c8* remove_dirs [DAG_TEST_MAX_INPUTS];
  spn_err_t expect_err;
  u32 expect_runs;
  u32 expect_requeues;
} build_t;

typedef struct {
  const c8* name;
  u32 workers;
  bool discovery;
  action_t actions [DAG_TEST_MAX_OPS];
  build_t builds [DAG_TEST_MAX_OPS];
} test_t;

typedef struct {
  dag_test_env_t dag;
  sp_atomic_s32_t runs;
} env_t;

typedef struct {
  env_t* env;
  spn_dag_t* g;
  const action_t* spec;
} ctx_t;

sp_test_suite(dag_parallel, .serial = true);

static const test_t tests [] = {
  {
    .name = "independent_actions_all_run",
    .actions = {
      { .identity = "A", .inputs = { "S" }, .output = "OA" },
      { .identity = "B", .inputs = { "S" }, .output = "OB" },
      { .identity = "C", .inputs = { "S" }, .output = "OC" },
      { .identity = "D", .inputs = { "S" }, .output = "OD" },
      { .identity = "E", .inputs = { "S" }, .output = "OE" },
      { .identity = "F", .inputs = { "S" }, .output = "OF" },
      { .identity = "G", .inputs = { "S" }, .output = "OG" },
    },
    .builds = {
      { .sources = { { "S", "1" } }, .expect_runs = 7 },
      { .sources = { { "S", "1" } }, .expect_runs = 7 },
    }
  },
  {
    .name = "chain_runs_in_dependency_order",
    .actions = {
      { .identity = "A", .inputs = { "S" }, .output = "X" },
      { .identity = "B", .inputs = { "X" }, .output = "Y" },
      { .identity = "C", .inputs = { "Y" }, .output = "Z" },
    },
    .builds = {
      { .sources = { { "S", "1" } }, .expect_runs = 3 },
      { .sources = { { "S", "1" } }, .expect_runs = 3 },
    }
  },
  {
    .name = "diamond_selective_rebuild",
    .actions = {
      { .identity = "A", .inputs = { "S" }, .output = "X" },
      { .identity = "B", .inputs = { "T" }, .output = "Y" },
      { .identity = "C", .inputs = { "X", "Y" }, .output = "Z" },
    },
    .builds = {
      { .sources = { { "S", "1" }, { "T", "1" } }, .expect_runs = 3 },
      { .sources = { { "S", "2" }, { "T", "1" } }, .expect_runs = 4 },
    }
  },
  {
    .name = "single_worker_completes",
    .workers = 1,
    .actions = {
      { .identity = "A", .inputs = { "S" }, .output = "X" },
      { .identity = "B", .inputs = { "X" }, .output = "Y" },
      { .identity = "C", .inputs = { "S" }, .output = "Z" },
    },
    .builds = {
      { .sources = { { "S", "1" } }, .expect_runs = 3 },
    }
  },
  {
    .name = "failing_action_stops_build",
    .actions = {
      { .identity = "A", .inputs = { "S" }, .output = "X", .fails = true },
      { .identity = "B", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "1" } }, .expect_err = SPN_ERR_DAG_ACTION },
    }
  },
  {
    .name = "cycle_fails",
    .actions = {
      { .identity = "A", .inputs = { "Y" }, .output = "X" },
      { .identity = "B", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .expect_err = SPN_ERR_DAG_STALLED },
    }
  },
  {
    .name = "discovered_generated_header_waits_for_producer",
    .discovery = true,
    .actions = {
      { .identity = "A", .inputs = { "S" }, .output = "H" },
      { .identity = "B", .inputs = { "M" }, .discovers = { "H" }, .output = "O" },
    },
    .builds = {
      { .sources = { { "S", "1" }, { "M", "1" } }, .expect_runs = 2, .expect_requeues = 1 },
      { .sources = { { "S", "1" }, { "M", "1" } }, .expect_runs = 2, .expect_requeues = 1 },
      { .sources = { { "S", "2" }, { "M", "1" } }, .expect_runs = 3, .expect_requeues = 1 },
    }
  },
  {
    .name = "discovered_tree_member_waits_for_producer",
    .discovery = true,
    .actions = {
      { .identity = "A", .inputs = { "S" }, .output = "D", .tree = true },
      { .identity = "B", .inputs = { "M" }, .discovers = { "D/H" }, .output = "O" },
    },
    .builds = {
      { .sources = { { "S", "1" }, { "M", "1" } }, .expect_runs = 2, .expect_requeues = 1 },
      { .sources = { { "S", "1" }, { "M", "1" } }, .remove_dirs = { "D" }, .expect_runs = 2, .expect_requeues = 1 },
    }
  },
  {
    .name = "tree_restored_after_delete",
    .actions = {
      { .identity = "A", .inputs = { "S" }, .output = "D", .tree = true },
      { .identity = "B", .inputs = { "S" }, .output = "O" },
    },
    .builds = {
      { .sources = { { "S", "1" } }, .expect_runs = 2 },
      { .sources = { { "S", "1" } }, .remove_dirs = { "D" }, .expect_runs = 2 },
    }
  },
};

static spn_err_t execute_graph(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, sp_mem_t mem, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  ctx_t* ctx = (ctx_t*)user_data;
  if (ctx->spec->fails) {
    return SPN_ERR_DAG_ACTION;
  }

  sp_carr_for(ctx->spec->discovers, it) {
    if (!ctx->spec->discovers[it]) {
      break;
    }
    spn_dag_observe(obs, (spn_dag_obs_t) {
      .kind = SPN_DAG_OBS_FILE,
      .path = spn_path_make(g->roots, sp_fs_join_path(mem, ctx->env->dag.root, sp_cstr_as_str(ctx->spec->discovers[it])))
    });
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  const spn_path_roots_t* roots = &ctx->env->dag.roots;

  sp_da_for(action->consumes, it) {
    spn_dag_artifact_t* in = spn_dag_find_artifact(ctx->g, action->consumes[it]);
    if (in->kind == SPN_DAG_ARTIFACT_KIND_FILE && !sp_fs_exists(spn_path_str(roots, s.mem, in->materialized))) {
      sp_mem_end_scratch(s);
      return SPN_ERR_DAG_ACTION;
    }
  }

  sp_atomic_s32_add(&ctx->env->runs, 1, SP_ATOMIC_SEQ_CST);
  spn_dag_artifact_t* out = spn_dag_find_artifact(ctx->g, action->produces[0]);
  sp_str_t content = sp_cstr_as_str(ctx->spec->identity);
  spn_path_t target = out->kind == SPN_DAG_ARTIFACT_KIND_TREE
    ? spn_path_join(s.mem, outputs[0], sp_str_lit("H"))
    : outputs[0];
  sp_err_t err = sp_fs_create_file_str(spn_path_str(roots, s.mem, target), content);
  sp_mem_end_scratch(s);
  return err ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static sp_err_t build_graph(sp_test_t* t, env_t* env, spn_dag_t* g, const test_t* test) {
  sp_carr_for(test->actions, ai) {
    const action_t* spec = &test->actions[ai];
    if (!spec->identity) {
      break;
    }

    ctx_t* ctx = sp_alloc_type(env->dag.mem, ctx_t);
    ctx->env = env;
    ctx->g = g;
    ctx->spec = spec;

    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .kind = spec->discovers[0] ? SPN_DAG_ACTION_DISCOVERED : SPN_DAG_ACTION_STATIC,
      .identity = dag_test_digest(spec->identity),
      .execute = execute_graph,
      .user_data = ctx
    });
    sp_carr_for(spec->inputs, ii) {
      if (!spec->inputs[ii]) {
        break;
      }
      spn_dag_action_add_input(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env->dag, sp_cstr_as_str(spec->inputs[ii]))));
    }
    spn_path_t output = dag_test_env_rooted(&env->dag, sp_cstr_as_str(spec->output));
    spn_dag_id_t out = spec->tree ? spn_dag_add_tree(g, output) : spn_dag_add_file(g, output);
    sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, out));
  }

  return SP_OK;
}

static sp_err_t check_expectations(sp_test_t* t, env_t* env, const test_t* test) {
  sp_carr_for(test->actions, ai) {
    const action_t* spec = &test->actions[ai];
    if (!spec->identity) {
      break;
    }
    sp_str_t path = dag_test_env_path(&env->dag, sp_cstr_as_str(spec->output));
    if (spec->tree) {
      path = sp_fs_join_path(env->dag.mem, path, sp_str_lit("H"));
    }
    sp_err_t err = dag_test_expect_file(t, env->dag.mem, path, spec->identity);
    if (err) {
      return err;
    }
  }
  return SP_OK;
}

static sp_err_t build_all(sp_test_t* t, spn_dag_store_kind_t kind, const test_t* test) {
  sp_test_kv_c(t, "store", dag_test_store_name(kind));

  env_t env = sp_zero;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) {
    .sub = dag_test_store_name(kind),
    .store = kind,
    .discovery = test->discovery
  });

  spn_thread_pool_t pool = sp_zero;
  spn_thread_pool_init(&pool, env.dag.mem, (spn_thread_pool_config_t) {
    .workers = test->workers ? test->workers : 4,
  });

  sp_err_t result = SP_OK;
  sp_carr_for(test->builds, b) {
    const build_t* build = &test->builds[b];
    if (!build->expect_runs && !build->expect_err) {
      break;
    }

    spn_dag_file_cache_invalidate_all(&env.dag.files);
    sp_carr_for(build->sources, si) {
      if (!build->sources[si].path) {
        break;
      }
      dag_test_env_create(&env.dag, sp_cstr_as_str(build->sources[si].path), sp_cstr_as_str(build->sources[si].content));
    }
    sp_carr_for(build->remove_dirs, si) {
      if (!build->remove_dirs[si]) {
        break;
      }
      sp_fs_remove_dir(dag_test_env_path(&env.dag, sp_cstr_as_str(build->remove_dirs[si])));
    }

    spn_dag_t* g = dag_test_env_graph(&env.dag);
    result = build_graph(t, &env, g, test);
    if (result) {
      break;
    }

    spn_err_t err = spn_dag_run_executor(g, &env.dag.env, &pool.executor);
    sp_expect_eq(t, build->expect_err, err);
    s32 runs = sp_atomic_s32_load(&env.runs, SP_ATOMIC_SEQ_CST);
    sp_expect_ge(t, runs, (s32)build->expect_runs);
    sp_expect_le(t, runs, (s32)(build->expect_runs + build->expect_requeues));

    if (!err && !build->expect_err) {
      result = check_expectations(t, &env, test);
      if (result) {
        break;
      }
    }
  }

  spn_thread_pool_deinit(&pool);
  return result;
}

sp_test_each(dag_parallel, builds, test_t, tests) {
  sp_carr_for(dag_test_store_kinds, kind) {
    sp_err_t err = build_all(t, dag_test_store_kinds[kind], it);
    if (err) {
      return err;
    }
  }
  return SP_OK;
}
