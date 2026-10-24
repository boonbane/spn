#include "dag/dag_test.h"

typedef struct {
  const c8* name;
  bool declared;
  spn_dag_store_kind_t store;
} resolve_test_t;

typedef struct {
  dag_test_env_t dag;
  spn_dag_id_t middle;
  spn_dag_id_t output;
  struct {
    bool roots_match;
    bool output_under_scratch;
    bool arrays_match;
    sp_str_t output_name;
    sp_str_t input_content;
  } seen;
} resolve_env_t;

static const resolve_test_t resolve_tests [] = {
  { .name = "store_blob_input", .store = SPN_DAG_STORE_FILESYSTEM },
  { .name = "declared_input_memory", .declared = true, .store = SPN_DAG_STORE_MEM },
  { .name = "declared_input_filesystem", .declared = true, .store = SPN_DAG_STORE_FILESYSTEM },
};

static spn_err_t produce(const si_dag_ctx_t* c, void* user_data) {
  resolve_env_t* env = (resolve_env_t*)user_data;
  return sp_fs_create_file_str_at(dag_test_at(&env->dag, si_dag_resolve_output(c, env->middle)), sp_str_lit("A")) ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static spn_err_t consume(const si_dag_ctx_t* c, void* user_data) {
  resolve_env_t* env = (resolve_env_t*)user_data;
  spn_path_t input = si_dag_resolve_input(c, env->middle);
  spn_path_t output = si_dag_resolve_output(c, env->output);
  env->seen.roots_match = c->roots == &env->dag.roots;
  env->seen.output_under_scratch = spn_path_equal(spn_path_parent(output), c->scratch);
  env->seen.arrays_match = sp_da_size(c->consumes) == 1 && sp_da_size(c->produces) == 1
    && spn_path_equal(si_dag_resolve_input(c, c->consumes[0]), input)
    && spn_path_equal(si_dag_resolve_output(c, c->produces[0]), output);
  env->seen.output_name = sp_str_copy(env->dag.mem, sp_fs_get_name(output.sub));
  sp_str_t content = sp_zero;
  if (sp_io_read_file_at(env->dag.mem, dag_test_at(&env->dag, input), &content)) {
    return SPN_ERR_DAG_ACTION;
  }
  env->seen.input_content = sp_str_copy(env->dag.mem, content);
  return sp_fs_create_file_str_at(dag_test_at(&env->dag, output), content) ? SPN_ERR_DAG_ACTION : SPN_OK;
}

sp_test_each(dag_action_ctx, resolve, resolve_test_t, resolve_tests) {
  resolve_env_t env = sp_zero;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) { .store = it->store });

  spn_dag_t* g = dag_test_env_graph(&env.dag);
  spn_dag_id_t producer = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = dag_test_digest("P"),
    .execute = produce,
    .user_data = &env
  });
  env.middle = it->declared
    ? spn_dag_add_file(g, dag_test_env_rooted(&env.dag, sp_str_lit("X")))
    : spn_dag_add_output(g, sp_str_lit("X"));
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, producer, env.middle));

  spn_dag_id_t consumer = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = dag_test_digest("C"),
    .execute = consume,
    .user_data = &env
  });
  spn_dag_action_add_input(g, consumer, env.middle);
  env.output = spn_dag_add_file(g, dag_test_env_rooted(&env.dag, sp_str_lit("Y")));
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, consumer, env.output));

  sp_expect_eq(t, SPN_OK, spn_dag_run(g, &env.dag.env));
  sp_expect(t, env.seen.roots_match);
  sp_expect(t, env.seen.output_under_scratch);
  sp_expect(t, env.seen.arrays_match);
  sp_expect_str_eq_c(t, env.seen.output_name, "Y");
  sp_expect_str_eq_c(t, env.seen.input_content, "A");
  return dag_test_expect_file(t, env.dag.mem, dag_test_env_path(&env.dag, sp_str_lit("Y")), "A");
}

typedef struct {
  const c8* name;
  bool progress;
  bool wake;
  u64 expect_warm;
  u32 expect_fired;
} warm_test_t;

typedef struct {
  dag_test_env_t dag;
  u32 fired;
  struct {
    u64 warm;
    u32 fired;
  } seen;
} warm_env_t;

static const warm_test_t warm_tests [] = {
  { .name = "adds_delta_and_rings", .progress = true, .wake = true, .expect_warm = 7, .expect_fired = 1 },
  { .name = "without_wake", .progress = true, .expect_warm = 7 },
  { .name = "without_progress", .wake = true },
};

static void count_wake(void* data) {
  ((warm_env_t*)data)->fired++;
}

static spn_err_t warm(const si_dag_ctx_t* c, void* user_data) {
  warm_env_t* env = (warm_env_t*)user_data;
  si_dag_add_warm(c, 7);
  env->seen.warm = sp_atomic_u64_load(&env->dag.progress.warm, SP_ATOMIC_SEQ_CST);
  env->seen.fired = env->fired;
  si_dag_add_warm(c, -7);
  return dag_test_exec_stamp(c, &env->dag);
}

sp_test_each(dag_action_ctx, warm, warm_test_t, warm_tests) {
  warm_env_t env = sp_zero;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) { .store = SPN_DAG_STORE_MEM });
  spn_wake_t wake = { .fn = count_wake, .data = &env };
  env.dag.env.progress = it->progress ? &env.dag.progress : SP_NULLPTR;
  env.dag.env.wake = it->wake ? &wake : SP_NULLPTR;

  spn_dag_t* g = dag_test_env_graph(&env.dag);
  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = dag_test_digest("W"),
    .execute = warm,
    .user_data = &env
  });
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env.dag, sp_str_lit("O")))));

  sp_expect_eq(t, SPN_OK, spn_dag_execute(g, action, &env.dag.env));
  sp_expect_eq(t, it->expect_warm, env.seen.warm);
  sp_expect_eq(t, it->expect_fired, env.seen.fired);
  sp_expect_eq(t, 0u, (u32)sp_atomic_u64_load(&env.dag.progress.warm, SP_ATOMIC_SEQ_CST));
  return SP_OK;
}

typedef struct {
  const c8* name;
  spn_dag_action_kind_t kind;
  bool expect_obs;
} obs_test_t;

typedef struct {
  dag_test_env_t dag;
  bool seen_obs;
} obs_env_t;

static const obs_test_t obs_tests [] = {
  { .name = "static", .kind = SPN_DAG_ACTION_STATIC },
  { .name = "discovered", .kind = SPN_DAG_ACTION_DISCOVERED, .expect_obs = true },
  { .name = "uncacheable", .kind = SPN_DAG_ACTION_UNCACHEABLE },
};

static spn_err_t observe(const si_dag_ctx_t* c, void* user_data) {
  obs_env_t* env = (obs_env_t*)user_data;
  env->seen_obs = c->obs != SP_NULLPTR;
  return dag_test_exec_stamp(c, &env->dag);
}

sp_test_each(dag_action_ctx, obs, obs_test_t, obs_tests) {
  obs_env_t env = sp_zero;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) { .store = SPN_DAG_STORE_MEM, .discovery = true });

  spn_dag_t* g = dag_test_env_graph(&env.dag);
  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .kind = it->kind,
    .identity = dag_test_digest("B"),
    .execute = observe,
    .user_data = &env
  });
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env.dag, sp_str_lit("O")))));

  sp_expect_eq(t, SPN_OK, spn_dag_execute(g, action, &env.dag.env));
  sp_expect_eq(t, it->expect_obs, env.seen_obs);
  return SP_OK;
}
