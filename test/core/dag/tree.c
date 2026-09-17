#include "dag_test.h"
#include "fs/fs.h"

typedef struct {
  const c8* path;
  const c8* content;
} tree_file_t;

typedef struct {
  u32 runs;
  u32 hashes;
  tree_file_t files [DAG_TEST_MAX_OUTPUTS];
  const c8* absent [DAG_TEST_MAX_OUTPUTS];
  const c8* kept [DAG_TEST_MAX_OUTPUTS];
} tree_expect_t;

typedef struct {
  const c8* identity;
  tree_file_t files [DAG_TEST_MAX_OUTPUTS];
  bool remove_target;
  tree_file_t write [DAG_TEST_MAX_OUTPUTS];
  tree_file_t poison;
  tree_expect_t expect;
} tree_run_t;

typedef struct {
  const c8* name;
  tree_run_t runs [DAG_TEST_MAX_OPS];
} tree_test_t;

typedef struct {
  dag_test_env_t dag;
  const tree_run_t* run;
} tree_env_t;

static const tree_test_t tree_tests [] = {
  {
    .name = "cold_hashes_once_per_entry",
    .runs = {
      { .identity = "I", .files = { { "X.h", "A" }, { "B/Y.h", "B" } }, .expect = { .runs = 1, .hashes = 2, .files = { { "X.h", "A" }, { "B/Y.h", "B" } } } },
    }
  },
  {
    .name = "hit_is_free",
    .runs = {
      { .identity = "I", .files = { { "X.h", "A" } }, .expect = { .runs = 1, .hashes = 1 } },
      { .identity = "I", .files = { { "X.h", "A" } }, .expect = { .runs = 1, .kept = { "X.h" } } },
    }
  },
  {
    .name = "deleted_target_relinked",
    .runs = {
      { .identity = "I", .files = { { "X.h", "A" } }, .expect = { .runs = 1, .hashes = 1 } },
      { .identity = "I", .files = { { "X.h", "A" } }, .remove_target = true, .expect = { .runs = 1, .files = { { "X.h", "A" } } } },
    }
  },
  {
    .name = "identity_change_reruns",
    .runs = {
      { .identity = "I", .files = { { "X.h", "A" } }, .expect = { .runs = 1, .hashes = 1 } },
      { .identity = "J", .files = { { "X.h", "B" } }, .expect = { .runs = 2, .hashes = 1, .files = { { "X.h", "B" } } } },
    }
  },
  {
    .name = "replaced_entry_hashed_once_and_relinked",
    .runs = {
      { .identity = "I", .files = { { "X.h", "A" }, { "B/Y.h", "B" } }, .expect = { .runs = 1, .hashes = 2 } },
      { .identity = "I", .files = { { "X.h", "A" }, { "B/Y.h", "B" } }, .write = { { "X.h", "T" } }, .expect = { .runs = 1, .hashes = 1, .files = { { "X.h", "A" } }, .kept = { "B/Y.h" } } },
    }
  },
  {
    .name = "stray_file_pruned",
    .runs = {
      { .identity = "I", .files = { { "X.h", "A" } }, .expect = { .runs = 1, .hashes = 1 } },
      { .identity = "I", .files = { { "X.h", "A" } }, .write = { { "Z.h", "S" } }, .expect = { .runs = 1, .absent = { "Z.h" }, .kept = { "X.h" } } },
    }
  },
  {
    .name = "stray_subdir_pruned",
    .runs = {
      { .identity = "I", .files = { { "X.h", "A" } }, .expect = { .runs = 1, .hashes = 1 } },
      { .identity = "I", .files = { { "X.h", "A" } }, .write = { { "Z/W.h", "S" } }, .expect = { .runs = 1, .absent = { "Z" }, .kept = { "X.h" } } },
    }
  },
  {
    .name = "dir_replaces_file",
    .runs = {
      { .identity = "I", .files = { { "B/Y.h", "B" } }, .expect = { .runs = 1, .hashes = 1 } },
      { .identity = "I", .files = { { "B/Y.h", "B" } }, .remove_target = true, .write = { { "B", "S" } }, .expect = { .runs = 1, .files = { { "B/Y.h", "B" } } } },
    }
  },
  {
    .name = "file_replaces_dir",
    .runs = {
      { .identity = "I", .files = { { "X.h", "A" } }, .expect = { .runs = 1, .hashes = 1 } },
      { .identity = "I", .files = { { "X.h", "A" } }, .remove_target = true, .write = { { "X.h/W", "S" } }, .expect = { .runs = 1, .files = { { "X.h", "A" } }, .absent = { "X.h/W" } } },
    }
  },
  {
    .name = "poisoned_blob_dropped_and_rerun",
    .runs = {
      { .identity = "I", .files = { { "X.h", "A" } }, .expect = { .runs = 1, .hashes = 1 } },
      { .identity = "I", .files = { { "X.h", "A" } }, .poison = { "X.h", "T" }, .expect = { .runs = 2, .hashes = 2, .files = { { "X.h", "A" } } } },
    }
  },
  {
    .name = "empty_tree_has_a_directory",
    .runs = {
      { .identity = "I", .expect = { .runs = 1 } },
      { .identity = "I", .remove_target = true, .expect = { .runs = 1 } },
    }
  },
};

static spn_err_t tree_exec(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* dag_env, sp_mem_t mem, sp_da(spn_dag_obs_t)* obs) {
  tree_env_t* env = (tree_env_t*)user_data;
  env->dag.runs++;
  spn_dag_artifact_t* out = spn_dag_find_artifact(env->dag.g, action->produces[0]);
  sp_fs_create_dir(dag_test_render(&env->dag, out->materialized));
  sp_carr_for(env->run->files, it) {
    if (!env->run->files[it].path) {
      break;
    }
    sp_str_t path = dag_test_render(&env->dag, spn_path_join(env->dag.mem, out->materialized, sp_cstr_as_str(env->run->files[it].path)));
    sp_fs_create_dir(sp_fs_parent_path(path));
    if (sp_fs_create_file_str(path, sp_cstr_as_str(env->run->files[it].content))) {
      return SPN_ERR_DAG_ACTION;
    }
  }
  return SPN_OK;
}

sp_test_each(dag_tree, exec, tree_test_t, tree_tests) {
  tree_env_t env = sp_zero;
  dag_test_env_init(&env.dag, t, (dag_test_env_config_t) { .store = SPN_DAG_STORE_FILESYSTEM });
  sp_mem_t mem = env.dag.mem;
  sp_str_t target = dag_test_env_path(&env.dag, sp_str_lit("install"));
  spn_path_t tree = dag_test_env_rooted(&env.dag, sp_str_lit("install"));

  sp_carr_for(it->runs, r) {
    const tree_run_t* run = &it->runs[r];
    if (!run->expect.runs) {
      break;
    }

    env.run = run;
    dag_test_env_cold(&env.dag);
    if (run->remove_target) {
      sp_fs_remove_dir(target);
    }
    sp_carr_for(run->write, wt) {
      if (!run->write[wt].path) {
        break;
      }
      dag_test_create(sp_fs_join_path(mem, target, sp_cstr_as_str(run->write[wt].path)), sp_cstr_as_str(run->write[wt].content));
    }
    if (run->poison.path) {
      sp_str_t path = sp_fs_join_path(mem, target, sp_cstr_as_str(run->poison.path));
      sp_must_ok(t, sp_fs_set_writable(path));
      sp_must_ok(t, sp_fs_create_file_str(path, sp_cstr_as_str(run->poison.content)));
    }

    sp_sys_file_meta_t before [DAG_TEST_MAX_OUTPUTS] = sp_zero;
    sp_carr_for(run->expect.kept, kt) {
      if (!run->expect.kept[kt]) {
        break;
      }
      sp_must_ok(t, sp_sys_get_path_metadata_s(sp_sys_get_root(0), sp_fs_join_path(mem, target, sp_cstr_as_str(run->expect.kept[kt])), &before[kt]));
    }
    u32 hashed = dag_test_hashed(&env.dag);

    spn_dag_t* g = dag_test_env_graph(&env.dag);
    spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
      .identity = dag_test_digest(run->identity),
      .execute = tree_exec,
      .user_data = &env
    });
    sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, spn_dag_add_tree(g, tree)));

    sp_expect_eq(t, SPN_OK, spn_dag_execute(g, action, &env.dag.env));
    sp_expect_eq(t, run->expect.runs, env.dag.runs);
    sp_expect_eq(t, run->expect.hashes, dag_test_hashed(&env.dag) - hashed);
    sp_expect(t, sp_fs_is_dir(target));

    sp_carr_for(run->expect.files, ft) {
      if (!run->expect.files[ft].path) {
        break;
      }
      sp_err_t err = dag_test_expect_file(t, mem, sp_fs_join_path(mem, target, sp_cstr_as_str(run->expect.files[ft].path)), run->expect.files[ft].content);
      if (err) {
        return err;
      }
    }
    sp_carr_for(run->expect.absent, at) {
      if (!run->expect.absent[at]) {
        break;
      }
      sp_expect(t, !sp_fs_exists(sp_fs_join_path(mem, target, sp_cstr_as_str(run->expect.absent[at]))));
    }
    sp_carr_for(run->expect.kept, kt) {
      if (!run->expect.kept[kt]) {
        break;
      }
      sp_sys_file_meta_t after = sp_zero;
      sp_must_ok(t, sp_sys_get_path_metadata_s(sp_sys_get_root(0), sp_fs_join_path(mem, target, sp_cstr_as_str(run->expect.kept[kt])), &after));
      sp_expect_eq(t, before[kt].id, after.id);
    }
  }

  return SP_OK;
}
