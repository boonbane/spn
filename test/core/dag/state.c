#include "dag/dag_test.h"

typedef enum {
  LOCATION_NONE,
  LOCATION_DECLARED,
  LOCATION_STORE,
} location_t;

typedef struct {
  const c8* name;
  const c8* artifact;
  struct {
    const c8* content;
    location_t location [2];
  } expect;
} fact_t;

typedef struct {
  dag_test_env_t* env;
  const c8* content;
} write_t;

static const fact_t facts [] = {
  {
    .name = "source",
    .artifact = "S",
    .expect = { .content = "A", .location = { LOCATION_DECLARED, LOCATION_DECLARED } }
  },
  {
    .name = "pathless_output",
    .artifact = "X",
    .expect = { .content = "B", .location = { [SPN_DAG_STORE_MEM] = LOCATION_NONE, [SPN_DAG_STORE_FILESYSTEM] = LOCATION_STORE } }
  },
  {
    .name = "declared_output",
    .artifact = "Y",
    .expect = { .content = "C", .location = { LOCATION_DECLARED, LOCATION_DECLARED } }
  },
};

static spn_err_t write_output(const si_dag_ctx_t* c, void* user_data) {
  write_t* write = (write_t*)user_data;
  return sp_fs_create_file_str_at(dag_test_at(write->env, si_dag_resolve_output(c, c->produces[0])), sp_cstr_as_str(write->content)) ? SPN_ERR_DAG_ACTION : SPN_OK;
}

static sp_err_t check_fact(sp_test_t* t, spn_dag_store_kind_t kind, const fact_t* fact) {
  sp_test_kv_c(t, "store", dag_test_store_name(kind));

  dag_test_env_t env;
  dag_test_env_init(&env, t, (dag_test_env_config_t) { .sub = dag_test_store_name(kind), .store = kind });
  dag_test_env_create(&env, sp_str_lit("S"), sp_str_lit("A"));
  write_t writes [] = {
    { .env = &env, .content = "B" },
    { .env = &env, .content = "C" },
  };

  spn_dag_t* g = dag_test_env_graph(&env);
  spn_dag_id_t s = spn_dag_add_file(g, dag_test_env_rooted(&env, sp_str_lit("S")));
  spn_dag_id_t x = spn_dag_add_output(g, sp_str_lit("X"));
  spn_dag_id_t y = spn_dag_add_file(g, dag_test_env_rooted(&env, sp_str_lit("Y")));
  spn_dag_id_t p = spn_dag_add_action(g, (spn_dag_action_config_t) { .identity = dag_test_digest("P"), .execute = write_output, .user_data = &writes[0] });
  spn_dag_id_t c = spn_dag_add_action(g, (spn_dag_action_config_t) { .identity = dag_test_digest("C"), .execute = write_output, .user_data = &writes[1] });
  spn_dag_action_add_input(g, p, s);
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, p, x));
  spn_dag_action_add_input(g, c, x);
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, c, y));

  sp_must_eq(t, SPN_OK, dag_test_run(&env));

  spn_dag_id_t ids [] = { s, x, y };
  const c8* names [] = { "S", "X", "Y" };
  spn_dag_id_t id = sp_zero;
  sp_carr_for(names, it) {
    if (sp_cstr_equal(names[it], fact->artifact)) {
      id = ids[it];
    }
  }
  sp_must(t, id.occupied);

  const spn_dag_artifact_state_t* artifact = spn_dag_run_artifact(&env.run, id);
  sp_expect(t, spn_dag_digest_equal(artifact->digest, dag_test_digest(fact->expect.content)));
  switch (fact->expect.location[kind]) {
    case LOCATION_NONE: {
      sp_expect(t, spn_path_empty(artifact->location));
      break;
    }
    case LOCATION_DECLARED: {
      sp_expect(t, spn_path_equal(artifact->location, dag_test_env_rooted(&env, sp_cstr_as_str(fact->artifact))));
      break;
    }
    case LOCATION_STORE: {
      spn_path_t blob = sp_zero;
      sp_must_eq(t, SPN_OK, spn_dag_store_locate(&env.store, env.mem, artifact->digest, sp_cstr_as_str(fact->artifact), &blob));
      sp_expect(t, spn_path_equal(artifact->location, blob));
      break;
    }
  }
  return SP_OK;
}

sp_test_each(dag_state, run, fact_t, facts) {
  sp_carr_for(dag_test_store_kinds, kind) {
    sp_err_t err = check_fact(t, dag_test_store_kinds[kind], it);
    if (err) {
      return err;
    }
  }
  return SP_OK;
}

typedef struct {
  const c8* name;
  const c8* source;
  struct {
    spn_err_t err;
    const c8* output;
  } expect;
} execute_t;

static const execute_t executes [] = {
  { .name = "seeds_sources", .source = "A", .expect = { .output = "A" } },
  { .name = "missing_source_fails", .expect = { .err = SPN_ERR_DAG_MISSING_INPUT } },
};

static spn_err_t copy_input(const si_dag_ctx_t* c, void* user_data) {
  dag_test_env_t* env = (dag_test_env_t*)user_data;
  return sp_fs_copy_file_at(dag_test_at(env, si_dag_resolve_input(c, c->consumes[0])), dag_test_at(env, si_dag_resolve_output(c, c->produces[0])), SP_FS_ATOMIC_REPLACE) ? SPN_ERR_DAG_ACTION : SPN_OK;
}

sp_test_each(dag_state, execute, execute_t, executes) {
  dag_test_env_t env;
  dag_test_env_init(&env, t, (dag_test_env_config_t) { .store = SPN_DAG_STORE_MEM });
  if (it->source) {
    dag_test_env_create(&env, sp_str_lit("S"), sp_cstr_as_str(it->source));
  }

  spn_dag_t* g = dag_test_env_graph(&env);
  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) { .identity = dag_test_digest("I"), .execute = copy_input, .user_data = &env });
  spn_dag_action_add_input(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env, sp_str_lit("S"))));
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env, sp_str_lit("O")))));

  sp_expect_eq(t, it->expect.err, dag_test_run(&env));
  sp_expect_eq(t, it->expect.err, env.run.diag.err);
  if (it->expect.output) {
    return dag_test_expect_file(t, env.mem, dag_test_env_path(&env, sp_str_lit("O")), it->expect.output);
  }
  return SP_OK;
}
