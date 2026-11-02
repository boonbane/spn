#include "dag/dag_test.h"
#include "stage/stage.h"

typedef struct {
  const c8* path;
  const c8* content;
} file_t;

typedef struct {
  const c8* key;
  const c8* from;
  const c8* to;
} row_t;

typedef struct {
  file_t files [4];
  row_t rows [4];
  const c8* declared [4];
  bool clean;
  spn_err_t expect_err;
  const c8* expect_path;
  file_t expect [4];
  const c8* missing [4];
} step_t;

typedef struct {
  const c8* name;
  step_t steps [3];
} test_t;

static const test_t tests [] = {
  {
    .name = "copies_rows_and_records_them",
    .steps = {
      {
        .files = { { "src/a", "A" } },
        .rows = { { "gen/a.txt", "src/a", "gen/a.txt" } },
        .declared = { "gen/a.txt" },
        .expect = { { "gen/a.txt", "A" }, { ".spn/staged", "gen/a.txt\tgen/a.txt\n" } },
      },
    },
  },
  {
    .name = "dropped_key_is_pruned_with_empty_parents",
    .steps = {
      {
        .files = { { "src/a", "A" } },
        .rows = { { "gen/a.txt", "src/a", "gen/a.txt" } },
        .declared = { "gen/a.txt" },
        .expect = { { "gen/a.txt", "A" } },
      },
      {
        .missing = { "gen/a.txt", "gen" },
        .expect = { { ".spn/staged", "" } },
      },
    },
  },
  {
    .name = "declared_key_not_applied_is_kept",
    .steps = {
      {
        .files = { { "src/a", "A" } },
        .rows = { { "A", "src/a", "gen/a.txt" } },
        .declared = { "A" },
      },
      {
        .declared = { "A" },
        .expect = { { "gen/a.txt", "A" }, { ".spn/staged", "A\tgen/a.txt\n" } },
      },
    },
  },
  {
    .name = "applied_key_replaces_its_rows",
    .steps = {
      {
        .files = { { "src/a", "A" }, { "src/b", "B" } },
        .rows = { { "K", "src/a", "gen/a.txt" }, { "K", "src/b", "gen/b.txt" } },
        .declared = { "K" },
        .expect = { { "gen/a.txt", "A" }, { "gen/b.txt", "B" } },
      },
      {
        .rows = { { "K", "src/a", "gen/a.txt" } },
        .declared = { "K" },
        .expect = { { "gen/a.txt", "A" } },
        .missing = { "gen/b.txt" },
      },
    },
  },
  {
    .name = "changed_source_is_recopied",
    .steps = {
      {
        .files = { { "src/a", "A" } },
        .rows = { { "K", "src/a", "gen/a.txt" } },
        .declared = { "K" },
        .expect = { { "gen/a.txt", "A" } },
      },
      {
        .files = { { "src/a", "B" } },
        .rows = { { "K", "src/a", "gen/a.txt" } },
        .declared = { "K" },
        .expect = { { "gen/a.txt", "B" } },
      },
    },
  },
  {
    .name = "foreign_file_at_destination_is_a_collision",
    .steps = {
      {
        .files = { { "src/a", "A" }, { "gen/a.txt", "X" } },
        .rows = { { "K", "src/a", "gen/a.txt" } },
        .declared = { "K" },
        .expect_err = SPN_ERR_STAGE_COLLISION,
        .expect_path = "gen/a.txt",
        .expect = { { "gen/a.txt", "X" } },
        .missing = { ".spn/staged" },
      },
    },
  },
  {
    .name = "identical_foreign_file_is_adopted",
    .steps = {
      {
        .files = { { "src/a", "A" }, { "gen/a.txt", "A" } },
        .rows = { { "K", "src/a", "gen/a.txt" } },
        .declared = { "K" },
        .expect = { { "gen/a.txt", "A" }, { ".spn/staged", "K\tgen/a.txt\n" } },
      },
    },
  },
  {
    .name = "shared_destination_survives_sibling_prune",
    .steps = {
      {
        .files = { { "src/l", "L" } },
        .rows = { { "A", "src/l", "gen/l" }, { "B", "src/l", "gen/l" } },
        .declared = { "A", "B" },
        .expect = { { "gen/l", "L" } },
      },
      {
        .rows = { { "A", "src/l", "gen/l" } },
        .declared = { "A" },
        .expect = { { "gen/l", "L" }, { ".spn/staged", "A\tgen/l\n" } },
      },
    },
  },
  {
    .name = "clean_removes_rows_and_manifest",
    .steps = {
      {
        .files = { { "src/a", "A" } },
        .rows = { { "K", "src/a", "gen/a.txt" } },
        .declared = { "K" },
        .expect = { { "gen/a.txt", "A" } },
      },
      {
        .clean = true,
        .missing = { "gen/a.txt", "gen", ".spn/staged", ".spn" },
      },
    },
  },
};

sp_test_each(dag_stage, steps, test_t, tests) {
  dag_test_env_t env;
  dag_test_env_init(&env, t, (dag_test_env_config_t) { .store = SPN_DAG_STORE_MEM });
  spn_path_t manifest = dag_test_env_rooted(&env, sp_str_lit(".spn/staged"));

  sp_carr_for(it->steps, st) {
    const step_t* step = &it->steps[st];
    if (st && !step->rows[0].key && !step->declared[0] && !step->clean && !step->expect[0].path && !step->missing[0]) {
      break;
    }

    spn_dag_file_cache_invalidate_all(&env.files);
    sp_carr_for(step->files, ft) {
      if (!step->files[ft].path) {
        break;
      }
      dag_test_env_create(&env, sp_cstr_as_str(step->files[ft].path), sp_cstr_as_str(step->files[ft].content));
    }

    si_da(spn_stage_t) rows = sp_zero;
    sp_carr_for(step->rows, rt) {
      const row_t* row = &step->rows[rt];
      if (!row->key) {
        break;
      }
      spn_stage_t staged = {
        .key = sp_cstr_as_str(row->key),
        .to = dag_test_env_rooted(&env, sp_cstr_as_str(row->to)),
        .location = dag_test_env_rooted(&env, sp_cstr_as_str(row->from)),
      };
      sp_must_eq(t, SPN_OK, spn_dag_file_cache_digest(&env.files, staged.location, &staged.digest));
      si_da_push(env.mem, rows, staged);
    }
    si_da(sp_str_t) declared = sp_zero;
    sp_carr_for(step->declared, dt) {
      if (!step->declared[dt]) {
        break;
      }
      si_da_push(env.mem, declared, sp_cstr_as_str(step->declared[dt]));
    }

    spn_stage_diag_t diag = step->clean
      ? spn_stage_clean(env.mem, &env.roots, manifest)
      : spn_stage_apply(env.mem, &env.roots, &env.files, manifest, declared, rows);
    sp_expect_eq(t, step->expect_err, diag.err);
    if (step->expect_path) {
      sp_expect(t, spn_path_equal(diag.path, dag_test_env_rooted(&env, sp_cstr_as_str(step->expect_path))));
    }

    sp_carr_for(step->expect, et) {
      if (!step->expect[et].path) {
        break;
      }
      sp_try(dag_test_expect_file(t, env.mem, dag_test_env_path(&env, sp_cstr_as_str(step->expect[et].path)), step->expect[et].content));
    }
    sp_carr_for(step->missing, mt) {
      if (!step->missing[mt]) {
        break;
      }
      sp_test_kv(t, "path", sp_cstr_as_str(step->missing[mt]));
      sp_expect(t, !sp_fs_exists_at(dag_test_env_path(&env, sp_cstr_as_str(step->missing[mt]))));
    }
  }
  return SP_OK;
}
