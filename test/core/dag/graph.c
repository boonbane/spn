#include "dag_test.h"

sp_test(dag_graph, second_producer_rejected) {
  spn_path_roots_t storage = sp_zero;
  const spn_path_roots_t* roots = paths_test_roots_build((paths_test_roots_t) { .project = "/R" }, &storage);
  spn_dag_t* g = spn_dag_new(sp_test_arena(t), roots);
  spn_dag_id_t a = spn_dag_add_action(g, (spn_dag_action_config_t) { .execute = dag_test_exec_noop });
  spn_dag_id_t b = spn_dag_add_action(g, (spn_dag_action_config_t) { .execute = dag_test_exec_noop });
  sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, a, spn_dag_add_file(g, spn_path_make(roots, sp_str_lit("/R/O")))));
  sp_expect_eq(t, SPN_ERR_DAG_DUPLICATE_OUTPUT, spn_dag_action_add_output(g, b, spn_dag_add_file(g, spn_path_make(roots, sp_str_lit("/R/O")))));
  return SP_OK;
}

sp_test(dag_graph, slashed_output_name_rejected) {
  spn_path_roots_t storage = sp_zero;
  spn_dag_t* g = spn_dag_new(sp_test_arena(t), paths_test_roots_build((paths_test_roots_t) sp_zero, &storage));
  spn_dag_id_t a = spn_dag_add_action(g, (spn_dag_action_config_t) { .execute = dag_test_exec_noop });
  sp_expect_eq(t, SPN_ERR_DAG_OUTPUT_NAME, spn_dag_action_add_output(g, a, spn_dag_add_output(g, sp_str_lit("sub/O"))));
  return SP_OK;
}

typedef struct {
  const c8* path;
  bool tree;
  u32 action;
} decl_t;

typedef struct {
  spn_err_t err;
  const c8* path;
} expect_t;

typedef struct {
  const c8* name;
  decl_t outputs [4];
  decl_t inputs [4];
  expect_t expect;
} test_t;

static const test_t tests [] = {
  {
    .name = "unrelated_paths_ok",
    .outputs = { { "/R/T", .tree = true }, { "/R/U/X", .action = 1 } },
    .inputs = { { "/R/S/Y" } },
  },
  {
    .name = "file_under_tree_rejected",
    .outputs = { { "/R/T", .tree = true }, { "/R/T/X", .action = 1 } },
    .expect = { .err = SPN_ERR_DAG_NESTED_OUTPUT, .path = "/R/T/X" },
  },
  {
    .name = "file_under_own_tree_rejected",
    .outputs = { { "/R/T", .tree = true }, { "/R/T/X" } },
    .expect = { .err = SPN_ERR_DAG_NESTED_OUTPUT, .path = "/R/T/X" },
  },
  {
    .name = "tree_under_tree_rejected",
    .outputs = { { "/R/T", .tree = true }, { "/R/T/U", .tree = true, .action = 1 } },
    .expect = { .err = SPN_ERR_DAG_NESTED_OUTPUT, .path = "/R/T/U" },
  },
  {
    .name = "deep_file_under_tree_rejected",
    .outputs = { { "/R/T", .tree = true }, { "/R/T/A/B/X", .action = 1 } },
    .expect = { .err = SPN_ERR_DAG_NESTED_OUTPUT, .path = "/R/T/A/B/X" },
  },
  {
    .name = "input_under_tree_rejected",
    .outputs = { { "/R/T", .tree = true, .action = 1 } },
    .inputs = { { "/R/T/X" } },
    .expect = { .err = SPN_ERR_DAG_NESTED_INPUT, .path = "/R/T/X" },
  },
  {
    .name = "path_claimed_as_file_and_tree_rejected",
    .outputs = { { "/R/P" }, { "/R/P", .tree = true, .action = 1 } },
    .expect = { .err = SPN_ERR_DAG_PATH_KIND, .path = "/R/P" },
  },
  {
    .name = "unproduced_tree_rejected",
    .inputs = { { "/R/T", .tree = true } },
    .expect = { .err = SPN_ERR_DAG_TREE_INPUT, .path = "/R/T" },
  },
  {
    .name = "tree_over_root_rejected",
    .outputs = { { "/R/C", .tree = true } },
    .expect = { .err = SPN_ERR_DAG_TREE_ROOT, .path = "/R/C" },
  },
};

sp_test_each(dag_graph, validate, test_t, tests) {
  spn_path_roots_t storage = sp_zero;
  const spn_path_roots_t* roots = paths_test_roots_build((paths_test_roots_t) { .project = "/R", .checkout = "/R/C/K" }, &storage);
  spn_dag_t* g = spn_dag_new(sp_test_arena(t), roots);
  spn_dag_id_t actions [2] = {
    spn_dag_add_action(g, (spn_dag_action_config_t) { .execute = dag_test_exec_noop }),
    spn_dag_add_action(g, (spn_dag_action_config_t) { .execute = dag_test_exec_noop }),
  };

  u32 outputs = 0;
  sp_carr_detect_len(it->outputs, outputs, it->outputs[outputs].path);
  sp_for(ot, outputs) {
    const decl_t* decl = &it->outputs[ot];
    spn_dag_id_t artifact = spn_dag_add_path(g, spn_path_make(roots, sp_str_view(decl->path)), decl->tree ? SPN_DAG_ARTIFACT_KIND_TREE : SPN_DAG_ARTIFACT_KIND_FILE);
    sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, actions[decl->action], artifact));
  }

  u32 inputs = 0;
  sp_carr_detect_len(it->inputs, inputs, it->inputs[inputs].path);
  sp_for(in, inputs) {
    const decl_t* decl = &it->inputs[in];
    spn_dag_id_t artifact = spn_dag_add_path(g, spn_path_make(roots, sp_str_view(decl->path)), decl->tree ? SPN_DAG_ARTIFACT_KIND_TREE : SPN_DAG_ARTIFACT_KIND_FILE);
    spn_dag_action_add_input(g, actions[0], artifact);
  }

  spn_dag_violation_t violation = spn_dag_validate(g);
  sp_expect_eq(t, it->expect.err, violation.err);
  if (it->expect.path) {
    sp_expect_str_eq_c(t, spn_path_str(roots, sp_test_arena(t), violation.path), it->expect.path);
  }
  return SP_OK;
}
