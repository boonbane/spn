#include "dag/dag_test.h"

typedef struct {
  dag_test_env_t dag;
  spn_path_t probe;
  spn_path_t seen;
} canonical_env_t;

static spn_err_t canonicalize(const si_dag_ctx_t* c, void* user_data) {
  canonical_env_t* env = (canonical_env_t*)user_data;
  env->seen = spn_path_copy(env->dag.mem, si_dag_canonicalize_path(c, env->probe));
  return dag_test_exec_stamp(c, &env->dag);
}

sp_test(dag_action_ctx_os, canonical_resolves_symlinked_head) {
  sp_test_skip_without_symlinks();

  canonical_env_t env = sp_zero;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) { .store = SPN_DAG_STORE_MEM });
  dag_test_env_create(&env.dag, sp_str_lit("real/H"), sp_str_lit("A"));
  sp_must_ok(t, sp_fs_create_sym_link_at(sp_str_lit("real"), dag_test_env_path(&env.dag, sp_str_lit("link")), SP_FS_KIND_DIR));
  env.probe = dag_test_env_rooted(&env.dag, sp_str_lit("link/H"));

  spn_dag_t* g = dag_test_env_graph(&env.dag);
  spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
    .identity = dag_test_digest("K"),
    .execute = canonicalize,
    .user_data = &env
  });
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env.dag, sp_str_lit("O")))));

  sp_expect_eq(t, SPN_OK, dag_test_run(&env.dag));
  sp_expect_eq(t, SPN_PATH_ROOT_PROJECT, env.seen.root);
  sp_expect_str_eq_c(t, env.seen.sub, "real/H");
  return SP_OK;
}
