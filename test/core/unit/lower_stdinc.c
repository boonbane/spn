#include "unit.h"

#include "dag/dag.h"
#include "graph/dag.h"
#include "graph/nodes/nodes.h"
#include "op/types.h"
#include "paths/paths.h"

#define lower_stdinc_actions_max 2
#define lower_stdinc_labels_max 2

typedef struct {
  si_dag_fn_t execute;
  const c8* consumes [lower_stdinc_labels_max];
} action_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t roots [UNIT_TEST_MAX_ROOTS];
  action_t expect [lower_stdinc_actions_max];
} test_t;

static const test_t tests [] = {
  {
    .name = "each_language_asks_its_own_launcher",
    .graph.pkgs = {
      { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c", "b.cpp" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = {
      { si_on_stdinc_search_list },
      { si_on_stdinc_search_list },
    },
  },
  {
    .name = "assembly_shares_the_c_question",
    .graph.pkgs = {
      { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c", "b.S" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = {
      { si_on_stdinc_search_list },
    },
  },
  {
    .name = "zig_libc_file_feeds_the_question",
    .graph = {
      .driver = SPN_CC_DRIVER_ZIG,
      .sdk_libc = true,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .expect = {
      { si_on_stdinc_fragment, .consumes = { "libc.txt" } },
    },
  },
  {
    .name = "clang_build_asks_nothing",
    .graph = {
      .driver = SPN_CC_DRIVER_CLANG,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
  },
};

sp_test_each(unit_lower_stdinc, actions, test_t, tests, .setup = spn_test_ctx_setup, .teardown = units_teardown) {
  sp_mem_t mem = sp_test_arena(t);
  units_t* u = units_new(t, &it->graph);
  u32 roots = 0;
  sp_carr_detect_len(it->roots, roots, it->roots[roots].name);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, it->roots, roots, &targets));
  sp_must_eq(t, SPN_OK, units_plan(u, targets));

  spn_op_t op = { .ctx = &u->ctx, .session = &u->session };
  spn_dag_build_t* b = spn_dag_build_new(&op);
  sp_must_eq(t, SPN_OK, si_dag_build_lower(b, &u->plan));
  sp_must_eq(t, SPN_OK, spn_dag_validate(b->graph).err);

  si_da(const spn_dag_action_t*) actions = sp_zero;
  sp_da_for(b->graph->actions, at) {
    const spn_dag_action_t* action = &b->graph->actions[at];
    if (action->execute == si_on_stdinc_fragment || action->execute == si_on_stdinc_search_list) {
      si_da_push(mem, actions, action);
    }
  }

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].execute);
  sp_must_eq(t, expected, (u32)si_da_size(actions));
  sp_for(at, expected) {
    const action_t* expect = &it->expect[at];
    const spn_dag_action_t* action = actions[at];
    sp_expect(t, action->execute == expect->execute);
    u32 consumes = 0;
    sp_carr_detect_len(expect->consumes, consumes, expect->consumes[consumes]);
    sp_must_eq(t, consumes, (u32)sp_da_size(action->consumes));
    sp_for(ct, consumes) {
      sp_expect_str_eq_c(t, spn_dag_find_artifact(b->graph, action->consumes[ct])->name, expect->consumes[ct]);
    }
  }
  return SP_OK;
}
