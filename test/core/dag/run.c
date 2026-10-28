#include "dag/dag_test.h"

typedef struct {
  const c8* path;
  const c8* content;
} source_t;

typedef struct {
  const c8* identity;
  const c8* inputs [DAG_TEST_MAX_INPUTS];
  const c8* orders [DAG_TEST_MAX_INPUTS];
  const c8* discovers [DAG_TEST_MAX_INPUTS];
  const c8* output;
  const c8* writes;
  bool tree;
  bool fails;
  bool skips_output;
  spn_dag_action_kind_t kind;
} action_t;

typedef struct {
  source_t sources [DAG_TEST_MAX_INPUTS];
  const c8* remove_dirs [DAG_TEST_MAX_INPUTS];
  spn_err_t expect_err;
  const c8* expect_diag_path;
  u32 expect_runs;
  u32 expect_hits;
  u32 expect_misses;
} build_t;

typedef struct {
  const c8* name;
  action_t actions [DAG_TEST_MAX_OPS];
  build_t builds [DAG_TEST_MAX_OPS];
} test_t;

typedef struct {
  dag_test_env_t* env;
  const action_t* spec;
} ctx_t;

static const test_t tests [] = {
  {
    .name = "chain_runs_in_dependency_order",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "second_build_all_hits",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "source_change_reruns_chain",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
      { .sources = { { "S", "B" } }, .expect_runs = 4 },
    }
  },
  {
    .name = "independent_actions_both_run",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "T" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "T", "B" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "diamond_selective_rebuild",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "T" }, .output = "Y" },
      { .identity = "K", .inputs = { "X", "Y" }, .output = "Z" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "T", "B" } }, .expect_runs = 3 },
      { .sources = { { "S", "C" }, { "T", "B" } }, .expect_runs = 5 },
    }
  },
  {
    .name = "missing_source_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
    },
    .builds = {
      { .expect_err = SPN_ERR_DAG_MISSING_INPUT, .expect_diag_path = "S" },
    }
  },
  {
    .name = "missing_output_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .skips_output = true },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_err = SPN_ERR_DAG_MISSING_OUTPUT, .expect_diag_path = "X", .expect_runs = 1 },
    }
  },
  {
    .name = "cycle_fails",
    .actions = {
      { .identity = "I", .inputs = { "Y" }, .output = "X" },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .expect_err = SPN_ERR_DAG_STALLED },
    }
  },
  {
    .name = "failing_action_stops_build",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .fails = true },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_err = SPN_ERR_DAG_ACTION },
    }
  },
  {
    .name = "counts_by_kind",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "S" }, .output = "Y", .kind = SPN_DAG_ACTION_UNCACHEABLE },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2, .expect_misses = 1 },
      { .sources = { { "S", "A" } }, .expect_runs = 3, .expect_hits = 1 },
    }
  },
  {
    .name = "uncacheable_stable_output_downstream_hits",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .writes = "C", .kind = SPN_DAG_ACTION_UNCACHEABLE },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" } }, .expect_runs = 3 },
    }
  },
  {
    .name = "uncacheable_changed_output_reruns_downstream",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_UNCACHEABLE },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" } }, .expect_runs = 4 },
    }
  },
  {
    .name = "order_only_input_waits_for_producer",
    .actions = {
      { .identity = "J", .inputs = { "T" }, .orders = { "X" }, .output = "Y" },
      { .identity = "I", .inputs = { "S" }, .output = "X" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "T", "B" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "order_only_producer_change_keeps_hit",
    .actions = {
      { .identity = "J", .inputs = { "T" }, .orders = { "X" }, .output = "Y" },
      { .identity = "I", .inputs = { "S" }, .output = "X" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "T", "B" } }, .expect_runs = 2 },
      { .sources = { { "S", "C" }, { "T", "B" } }, .expect_runs = 3 },
      { .sources = { { "S", "C" }, { "T", "B" } }, .expect_runs = 3 },
    }
  },
  {
    .name = "order_only_observed_change_reruns",
    .actions = {
      { .identity = "J", .inputs = { "M" }, .orders = { "X" }, .discovers = { "X" }, .output = "O" },
      { .identity = "I", .inputs = { "S" }, .output = "X" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "M", "B" } }, .expect_runs = 2 },
      { .sources = { { "S", "C" }, { "M", "B" } }, .expect_runs = 4 },
    }
  },
  {
    .name = "order_only_source_needs_no_producer",
    .actions = {
      { .identity = "J", .inputs = { "T" }, .orders = { "S" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "T", "B" } }, .expect_runs = 1 },
    }
  },
  {
    .name = "discovered_generated_header_waits_for_producer",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "H" },
      { .identity = "J", .inputs = { "M" }, .discovers = { "H" }, .output = "O" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "M", "B" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" }, { "M", "B" } }, .expect_runs = 2 },
      { .sources = { { "S", "C" }, { "M", "B" } }, .expect_runs = 4 },
    }
  },
  {
    .name = "discovered_tree_member_waits_for_producer",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "D", .tree = true },
      { .identity = "J", .inputs = { "M" }, .discovers = { "D/H" }, .output = "O" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "M", "B" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" }, { "M", "B" } }, .remove_dirs = { "D" }, .expect_runs = 2 },
    }
  },
  {
    .name = "discovered_source_header_no_deferral",
    .actions = {
      { .identity = "I", .inputs = { "M" }, .discovers = { "H" }, .output = "O" },
    },
    .builds = {
      { .sources = { { "M", "A" }, { "H", "B" } }, .expect_runs = 1 },
      { .sources = { { "M", "A" }, { "H", "B" } }, .expect_runs = 1 },
      { .sources = { { "M", "A" }, { "H", "C" } }, .expect_runs = 2 },
    }
  },
};

static spn_err_t execute_action(const si_dag_ctx_t* c, void* user_data) {
  ctx_t* ctx = (ctx_t*)user_data;
  if (ctx->spec->fails) {
    return SPN_ERR_DAG_ACTION;
  }

  sp_carr_for(ctx->spec->discovers, it) {
    if (!ctx->spec->discovers[it]) {
      break;
    }
    si_dag_observe(c, (spn_dag_obs_t) {
      .kind = SPN_DAG_OBS_FILE,
      .path = dag_test_env_rooted(ctx->env, sp_str_view(ctx->spec->discovers[it]))
    });
  }

  if (ctx->spec->skips_output) {
    ctx->env->runs++;
    return SPN_OK;
  }
  sp_da_for(c->consumes, it) {
    if (!sp_fs_exists_at(dag_test_at(ctx->env, si_dag_resolve_input(c, c->consumes[it])))) {
      return SPN_ERR_DAG_ACTION;
    }
  }
  sp_carr_for(ctx->spec->orders, it) {
    if (!ctx->spec->orders[it]) {
      break;
    }
    if (!sp_fs_exists_at(dag_test_at(ctx->env, dag_test_env_rooted(ctx->env, sp_str_view(ctx->spec->orders[it]))))) {
      return SPN_ERR_DAG_ACTION;
    }
  }
  ctx->env->runs++;
  spn_path_t output = si_dag_resolve_output(c, c->produces[0]);
  sp_str_t content = ctx->spec->writes
    ? sp_str_view(ctx->spec->writes)
    : sp_fmt(ctx->env->mem, "{}", sp_fmt_uint(ctx->env->runs)).value;
  if (ctx->spec->tree) {
    spn_path_t inside = spn_path_join(ctx->env->mem, output, sp_str_lit("H"));
    return sp_fs_create_file_str_at(dag_test_at(ctx->env, inside), sp_str_lit("T")) ? SPN_ERR_DAG_ACTION : SPN_OK;
  }
  return sp_fs_create_file_str_at(dag_test_at(ctx->env, output), content) ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static sp_err_t build_graph(sp_test_t* t, dag_test_env_t* env, spn_dag_t* g, const test_t* test) {
  sp_carr_for(test->actions, ai) {
    const action_t* spec = &test->actions[ai];
    if (!spec->identity) {
      break;
    }

    ctx_t* ctx = sp_alloc_type(env->mem, ctx_t);
    ctx->env = env;
    ctx->spec = spec;

    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .kind = spec->discovers[0] ? SPN_DAG_ACTION_DISCOVERED : spec->kind,
      .identity = dag_test_digest(spec->identity),
      .execute = execute_action,
      .user_data = ctx
    });
    sp_carr_for(spec->inputs, ii) {
      if (!spec->inputs[ii]) {
        break;
      }
      spn_dag_action_add_input(g, action, spn_dag_add_file(g, dag_test_env_rooted(env, sp_str_view(spec->inputs[ii]))));
    }
    sp_carr_for(spec->orders, oi) {
      if (!spec->orders[oi]) {
        break;
      }
      spn_dag_action_add_order(g, action, spn_dag_add_file(g, dag_test_env_rooted(env, sp_str_view(spec->orders[oi]))));
    }
    spn_path_t output = dag_test_env_rooted(env, sp_str_view(spec->output));
    spn_dag_id_t out_id = spec->tree ? spn_dag_add_tree(g, output) : spn_dag_add_file(g, output);
    sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, out_id));
  }

  return SP_OK;
}

sp_test_each(dag_run, builds, test_t, tests) {
  dag_test_env_t env;
  dag_test_env_init(&env, t, (dag_test_env_config_t) { .store = SPN_DAG_STORE_MEM });

  sp_carr_for(it->builds, b) {
    const build_t* build = &it->builds[b];
    if (!build->expect_runs && !build->expect_err) {
      break;
    }

    spn_dag_file_cache_invalidate_all(&env.files);
    sp_carr_for(build->sources, si) {
      if (!build->sources[si].path) {
        break;
      }
      sp_fs_create_file_str_at(dag_test_env_path(&env, sp_str_view(build->sources[si].path)), sp_str_view(build->sources[si].content));
    }
    sp_carr_for(build->remove_dirs, si) {
      if (!build->remove_dirs[si]) {
        break;
      }
      sp_fs_remove_dir_at(dag_test_env_path(&env, sp_str_view(build->remove_dirs[si])));
    }

    spn_dag_t* g = dag_test_env_graph(&env);
    sp_carr_for(it->actions, ai) {
      const action_t* spec = &it->actions[ai];
      if (!spec->identity) {
        break;
      }

      ctx_t* ctx = sp_alloc_type(env.mem, ctx_t);
      ctx->env = &env;
      ctx->spec = spec;

      spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .kind = spec->discovers[0] ? SPN_DAG_ACTION_DISCOVERED : spec->kind,
        .identity = dag_test_digest(spec->identity),
        .execute = execute_action,
        .user_data = ctx
      });
      sp_carr_for(spec->inputs, ii) {
        if (!spec->inputs[ii]) {
          break;
        }
        spn_dag_action_add_input(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env, sp_str_view(spec->inputs[ii]))));
      }
      sp_carr_for(spec->orders, oi) {
        if (!spec->orders[oi]) {
          break;
        }
        spn_dag_action_add_order(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env, sp_str_view(spec->orders[oi]))));
      }
      spn_path_t output = dag_test_env_rooted(&env, sp_str_view(spec->output));
      spn_dag_id_t out_id = spec->tree ? spn_dag_add_tree(g, output) : spn_dag_add_file(g, output);
      sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, out_id));
    }

    spn_err_t err = dag_test_run(&env);
    sp_expect_eq(t, build->expect_err, err);
    sp_expect_eq(t, build->expect_err, env.run.diag.err);
    if (build->expect_diag_path) {
      sp_expect(t, spn_path_equal(env.run.diag.path, dag_test_env_rooted(&env, sp_str_view(build->expect_diag_path))));
    }
    sp_expect_eq(t, build->expect_runs, env.runs);
    if (build->expect_hits || build->expect_misses) {
      sp_expect_eq(t, build->expect_hits, (u32)sp_atomic_s32_load(&env.run.progress.hits, SP_ATOMIC_SEQ_CST));
      sp_expect_eq(t, build->expect_misses, (u32)sp_atomic_s32_load(&env.run.progress.misses, SP_ATOMIC_SEQ_CST));
    }
  }

  return SP_OK;
}
