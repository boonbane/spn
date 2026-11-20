#include "unit.h"

#include "dag/dag.h"
#include "graph/dag.h"
#include "op/types.h"
#include "paths/paths.h"

#define LOWER_STAGE_ROWS_TEST_MAX_ROWS 4

typedef struct {
  const c8* key;
  const c8* to;
  const c8* output;
  const c8* file;
} row_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t roots [UNIT_TEST_MAX_ROOTS];
  const c8* present [UNIT_TEST_MAX_FILES];
  row_t expect [LOWER_STAGE_ROWS_TEST_MAX_ROWS];
} test_t;

static const test_t tests [] = {
  {
    .name = "toolchain_file_is_a_source_staged_under_the_exe_key",
    .graph = {
      .sanitize = SPN_SANITIZER_ADDRESS,
      .files.sanitizers = { { SPN_SANITIZER_ADDRESS, { "T/D" } } },
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .present = { "T/D" },
    .expect = {
      { "A", "build/debug/A", .output = "A" },
      { "A", "build/debug/D", .file = "T/D" },
    },
  },
};

sp_test_each(unit_lower_stage_rows, closure, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  units_t* u = units_new(t, &it->graph);
  u32 roots = 0;
  sp_carr_detect_len(it->roots, roots, it->roots[roots].name);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, it->roots, roots, &targets));
  sp_try(units_write_files(t, u, it->present, UNIT_TEST_MAX_FILES));
  sp_must_eq(t, SPN_OK, units_plan(u, targets));

  spn_op_t op = { .ctx = &u->ctx, .session = &u->session };
  spn_dag_build_t* b = spn_dag_build_new(&op);
  sp_must_eq(t, SPN_OK, si_dag_build_lower(b, &u->plan));

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].key);
  sp_must_eq(t, expected, (u32)si_da_size(b->stages.profile));
  sp_for(st, expected) {
    const row_t* expect = &it->expect[st];
    const spn_dag_stage_t* stage = &b->stages.profile[st];
    const spn_dag_artifact_t* artifact = spn_dag_find_artifact(b->graph, stage->artifact);
    sp_test_kv_c(t, "to", expect->to);
    sp_expect_str_eq_c(t, stage->key, expect->key);
    sp_expect_str_eq_c(t, stage->to.sub, expect->to);
    if (expect->file) {
      sp_expect(t, !artifact->producer.occupied);
      sp_expect(t, spn_path_equal(artifact->path, units_toolchain_file(u, expect->file)));
    }
    else {
      sp_expect(t, artifact->producer.occupied);
      sp_expect_str_eq_c(t, artifact->name, expect->output);
    }
  }
  return SP_OK;
}
