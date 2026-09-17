#include "dag/dag_test.h"

typedef enum {
  EXEC_BEHAVIOR_WRITE,
  EXEC_BEHAVIOR_FAIL,
  EXEC_BEHAVIOR_SKIP_LAST_OUTPUT,
} behavior_t;

typedef enum {
  EXEC_OP_DONE,
  EXEC_OP_RUN,
  EXEC_OP_REMOVE_OUTPUTS,
} op_kind_t;

typedef struct {
  const c8* identity;
  const c8* inputs [DAG_TEST_MAX_INPUTS];
  const c8* outputs [DAG_TEST_MAX_OUTPUTS];
  const c8* write [DAG_TEST_MAX_OUTPUTS];
  spn_dag_action_kind_t kind;
} action_t;

typedef struct {
  const c8* identity;
  const c8* inputs [DAG_TEST_MAX_INPUTS];
  const c8* outputs [DAG_TEST_MAX_OUTPUTS];
} change_t;

typedef struct {
  spn_err_t err;
  u32 runs;
  u32 hashes;
  const c8* contents [DAG_TEST_MAX_OUTPUTS];
} expect_t;

typedef struct {
  op_kind_t kind;
  behavior_t behavior;
  change_t change;
  const c8* unavailable [DAG_TEST_MAX_OUTPUTS];
  expect_t expect;
} op_t;

typedef struct {
  const c8* name;
  action_t action;
  op_t ops [DAG_TEST_MAX_OPS];
} test_t;

typedef struct {
  dag_test_env_t dag;
  spn_err_t err;
} env_t;

typedef struct {
  spn_dag_t* g;
  const action_t* spec;
  behavior_t behavior;
  env_t* env;
} ctx_t;

static const test_t tests [] = {
  {
    .name = "miss_executes_action",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 1, .contents = { "V1" } } },
    }
  },
  {
    .name = "hit_restores_deleted_output",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 1 } },
      { .kind = EXEC_OP_REMOVE_OUTPUTS },
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .contents = { "V1" } } },
    }
  },
  {
    .name = "settled_hit_hashes_nothing",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 1 } },
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .contents = { "V1" } } },
    }
  },
  {
    .name = "input_change_reruns",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 1 } },
      { .kind = EXEC_OP_RUN, .change = { .inputs = { "B" } }, .expect = { .runs = 2, .hashes = 1, .contents = { "V2" } } },
    }
  },
  {
    .name = "identity_change_reruns",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 1 } },
      { .kind = EXEC_OP_RUN, .change = { .identity = "J" }, .expect = { .runs = 2, .hashes = 1, .contents = { "V2" } } },
    }
  },
  {
    .name = "output_path_change_reruns",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 1 } },
      { .kind = EXEC_OP_RUN, .change = { .outputs = { "P" } }, .expect = { .runs = 2, .hashes = 1, .contents = { "V2" } } },
    }
  },
  {
    .name = "reverted_input_hits_prior_entry",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 1 } },
      { .kind = EXEC_OP_RUN, .change = { .inputs = { "B" } }, .expect = { .runs = 2, .hashes = 1 } },
      { .kind = EXEC_OP_RUN, .change = { .inputs = { "A" } }, .expect = { .runs = 2, .contents = { "V1" } } },
    }
  },
  {
    .name = "multiple_outputs_restored",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O", "P" }, .write = { "V", "W" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 2 } },
      { .kind = EXEC_OP_REMOVE_OUTPUTS },
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .contents = { "V1", "W1" } } },
    }
  },
  {
    .name = "failed_action_not_cached",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .behavior = EXEC_BEHAVIOR_FAIL, .expect = { .err = SPN_ERR_DAG_ACTION } },
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 1, .contents = { "V1" } } },
    }
  },
  {
    .name = "missing_output_not_cached",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O", "P" }, .write = { "V", "W" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .behavior = EXEC_BEHAVIOR_SKIP_LAST_OUTPUT, .expect = { .err = SPN_ERR_DAG_MISSING_OUTPUT, .runs = 1, .hashes = 1 } },
      { .kind = EXEC_OP_RUN, .expect = { .runs = 2, .hashes = 2, .contents = { "V2", "W2" } } },
    }
  },
  {
    .name = "uncacheable_always_executes",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" }, .kind = SPN_DAG_ACTION_UNCACHEABLE },
    .ops = {
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .hashes = 1, .contents = { "V1" } } },
      { .kind = EXEC_OP_RUN, .expect = { .runs = 2, .hashes = 1, .contents = { "V2" } } },
    }
  },
  {
    .name = "unavailable_cached_output_reruns_then_hits",
    .action = { .identity = "I", .inputs = { "A" }, .outputs = { "O" }, .write = { "V" } },
    .ops = {
      { .kind = EXEC_OP_RUN, .unavailable = { "U" }, .expect = { .runs = 1, .hashes = 1, .contents = { "V1" } } },
      { .kind = EXEC_OP_RUN, .expect = { .runs = 1, .contents = { "V1" } } },
    }
  },
};

static spn_err_t execute_action(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, sp_mem_t mem, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  ctx_t* ctx = (ctx_t*)user_data;
  if (ctx->behavior == EXEC_BEHAVIOR_FAIL) {
    return SPN_ERR_DAG_ACTION;
  }

  ctx->env->dag.runs++;
  u64 count = sp_da_size(action->produces);
  sp_da_for(action->produces, it) {
    if (ctx->behavior == EXEC_BEHAVIOR_SKIP_LAST_OUTPUT && it + 1 == count) {
      continue;
    }
    sp_str_t content = sp_fmt(ctx->env->dag.mem, "{}{}", sp_fmt_cstr(ctx->spec->write[it]), sp_fmt_uint(ctx->env->dag.runs)).value;
    if (sp_fs_create_file_str(dag_test_render(&ctx->env->dag, outputs[it]), content)) {
      return SPN_ERR_DAG_ACTION;
    }
  }
  return SPN_OK;
}

static void apply_change(action_t* action, change_t change) {
  if (change.identity) {
    action->identity = change.identity;
  }
  if (change.inputs[0]) {
    sp_carr_for(action->inputs, it) {
      action->inputs[it] = change.inputs[it];
    }
  }
  if (change.outputs[0]) {
    sp_carr_for(action->outputs, it) {
      action->outputs[it] = change.outputs[it];
    }
  }
}

static sp_err_t run_action(sp_test_t* t, env_t* env, const action_t* spec, const op_t* op) {
  spn_dag_t* g = dag_test_env_graph(&env->dag);
  ctx_t ctx = {
    .g = g,
    .spec = spec,
    .behavior = op->behavior,
    .env = env
  };

  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .kind = spec->kind,
    .identity = dag_test_digest(spec->identity),
    .execute = execute_action,
    .user_data = &ctx
  });

  sp_carr_for(spec->inputs, it) {
    if (!spec->inputs[it]) {
      break;
    }
    sp_str_t str = sp_str_view(spec->inputs[it]);
    spn_dag_action_add_input(g, action, spn_dag_add_value(g, str.data, str.len));
  }

  sp_carr_for(spec->outputs, it) {
    if (!spec->outputs[it]) {
      break;
    }
    spn_dag_id_t file = spn_dag_add_file(g, dag_test_env_rooted(&env->dag, sp_str_view(spec->outputs[it])));
    sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, file));
  }

  if (op->unavailable[0]) {
    spn_dag_action_output_t outputs [DAG_TEST_MAX_OUTPUTS] = sp_zero;
    spn_dag_action_t* a = spn_dag_find_action(g, action);
    u32 count = 0;
    sp_carr_for(op->unavailable, it) {
      if (!op->unavailable[it]) {
        break;
      }
      spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, a->produces[it]);
      outputs[it] = (spn_dag_action_output_t) {
        .name = artifact->name,
        .digest = dag_test_digest(op->unavailable[it])
      };
      count++;
    }
    spn_dag_action_cache_put(&env->dag.cache, spn_dag_weak_key(g, action), outputs, count);
  }

  u32 hashed = dag_test_hashed(&env->dag);
  env->err = spn_dag_execute(g, action, &env->dag.env);
  sp_expect_eq(t, op->expect.err, env->err);
  if (env->err != op->expect.err) {
    return SP_OK;
  }

  sp_expect_eq(t, op->expect.runs, env->dag.runs);
  sp_expect_eq(t, op->expect.hashes, dag_test_hashed(&env->dag) - hashed);
  if (env->err) {
    return SP_OK;
  }

  sp_carr_for(op->expect.contents, it) {
    if (!op->expect.contents[it]) {
      break;
    }
    sp_err_t err = dag_test_expect_file(t, env->dag.mem, dag_test_env_path(&env->dag, sp_str_view(spec->outputs[it])), op->expect.contents[it]);
    if (err) {
      return err;
    }

    spn_dag_action_t* a = spn_dag_find_action(g, action);
    spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, a->produces[it]);
    sp_must(t, spn_dag_digest_valid(artifact->digest));
    sp_expect(t, spn_dag_digest_equal(artifact->digest, dag_test_digest(op->expect.contents[it])));
  }

  return SP_OK;
}

static sp_err_t remove_outputs(sp_test_t* t, env_t* env, const action_t* action) {
  sp_carr_for(action->outputs, it) {
    if (!action->outputs[it]) {
      break;
    }
    spn_path_t rooted = dag_test_env_rooted(&env->dag, sp_str_view(action->outputs[it]));
    sp_str_t path = dag_test_render(&env->dag, rooted);
    env->err = sp_fs_remove_file(path) ? SPN_ERROR : SPN_OK;
    sp_expect_eq(t, SPN_OK, env->err);
    if (env->err) {
      return SP_OK;
    }
    spn_dag_file_cache_invalidate(&env->dag.files, rooted);
    sp_expect(t, !sp_fs_exists(path));
  }
  return SP_OK;
}

static sp_err_t run_ops(sp_test_t* t, spn_dag_store_kind_t kind, const test_t* test) {
  sp_test_kv_c(t, "store", dag_test_store_name(kind));

  env_t env = sp_zero;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) {
    .sub = dag_test_store_name(kind),
    .store = kind
  });
  action_t action = test->action;

  sp_carr_for(test->ops, it) {
    op_t op = test->ops[it];
    if (op.kind == EXEC_OP_DONE) {
      break;
    }

    sp_err_t err = SP_OK;
    switch (op.kind) {
      case EXEC_OP_DONE: {
        break;
      }
      case EXEC_OP_RUN: {
        apply_change(&action, op.change);
        err = run_action(t, &env, &action, &op);
        break;
      }
      case EXEC_OP_REMOVE_OUTPUTS: {
        err = remove_outputs(t, &env, &action);
        break;
      }
    }
    if (err) {
      return err;
    }

    if (env.err != op.expect.err) {
      break;
    }
  }

  return SP_OK;
}

sp_test_each(dag_exec, ops, test_t, tests) {
  sp_carr_for(dag_test_store_kinds, kind) {
    sp_err_t err = run_ops(t, dag_test_store_kinds[kind], it);
    if (err) {
      return err;
    }
  }
  return SP_OK;
}
