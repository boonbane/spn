#include "dag/dag_test.h"

sp_test(dag_trust, metadata_pinned_until_refresh) {
  dag_test_env_t env;
  dag_test_env_init(&env, t, (dag_test_env_config_t) sp_zero);
  spn_path_t path = dag_test_env_rooted(&env, sp_str_lit("F"));
  spn_dag_digest_t digest = sp_zero;

  dag_test_env_create(&env, sp_str_lit("F"), sp_str_lit("A"));
  sp_must_eq(t, SPN_OK, spn_dag_file_cache_digest(&env.files, path, &digest));
  sp_expect(t, spn_dag_digest_equal(digest, dag_test_digest("A")));

  u32 hashed = dag_test_hashed(&env);
  dag_test_env_create(&env, sp_str_lit("F"), sp_str_lit("BB"));
  sp_must_eq(t, SPN_OK, spn_dag_file_cache_digest(&env.files, path, &digest));
  sp_expect(t, spn_dag_digest_equal(digest, dag_test_digest("A")));
  sp_expect_eq(t, hashed, dag_test_hashed(&env));

  spn_dag_file_cache_invalidate_all(&env.files);
  sp_must_eq(t, SPN_OK, spn_dag_file_cache_digest(&env.files, path, &digest));
  sp_expect(t, spn_dag_digest_equal(digest, dag_test_digest("BB")));
  return SP_OK;
}

sp_test(dag_trust, seeded_digest_without_hash) {
  dag_test_env_t env;
  dag_test_env_init(&env, t, (dag_test_env_config_t) sp_zero);
  spn_path_t path = dag_test_env_rooted(&env, sp_str_lit("a.c"));

  dag_test_env_create(&env, sp_str_lit("a.c"), sp_str_lit("A"));
  sp_must_eq(t, SPN_OK, spn_dag_file_cache_seed(&env.files, path, dag_test_digest("B")));

  u32 hashed = dag_test_hashed(&env);
  spn_dag_digest_t digest = sp_zero;
  sp_must_eq(t, SPN_OK, spn_dag_file_cache_digest(&env.files, path, &digest));
  sp_expect(t, spn_dag_digest_equal(digest, dag_test_digest("B")));
  sp_expect_eq(t, hashed, dag_test_hashed(&env));
  return SP_OK;
}
