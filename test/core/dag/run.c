#include "dag/dag_test.h"

typedef struct {
  const c8* path;
  const c8* content;
} source_t;

typedef struct {
  spn_dag_obs_kind_t kind;
  const c8* path;
  const c8* filter;
} obs_t;

typedef struct {
  const c8* path;
  bool tree;
} claim_t;

typedef struct {
  const c8* identity;
  const c8* inputs [DAG_TEST_MAX_INPUTS];
  const c8* output;
  const c8* writes;
  bool tree;
  bool fails;
  bool skips_output;
  spn_dag_action_kind_t kind;
} action_t;

typedef struct {
  source_t sources [DAG_TEST_MAX_INPUTS];
  const c8* remove_dirs [DAG_TEST_MAX_INPUTS];
  obs_t observes [DAG_TEST_MAX_INPUTS];
  claim_t staged [DAG_TEST_MAX_INPUTS];
  spn_err_t expect_err;
  const c8* expect_diag_path;
  u32 expect_runs;
} build_t;

typedef struct {
  const c8* name;
  action_t actions [DAG_TEST_MAX_OPS];
  build_t builds [DAG_TEST_MAX_OPS];
} test_t;

typedef struct {
  dag_test_env_t* env;
  spn_dag_t* g;
  const action_t* spec;
  const build_t* build;
} ctx_t;

static const test_t tests [] = {
  {
    .name = "chain_runs_in_dependency_order",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "second_build_all_hits",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "source_change_reruns_chain",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
      { .sources = { { "S", "B" } }, .expect_runs = 4 },
    }
  },
  {
    .name = "independent_actions_both_run",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "T" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "T", "B" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "diamond_selective_rebuild",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
      { .identity = "J", .inputs = { "T" }, .output = "Y" },
      { .identity = "K", .inputs = { "X", "Y" }, .output = "Z" },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "T", "B" } }, .expect_runs = 3 },
      { .sources = { { "S", "C" }, { "T", "B" } }, .expect_runs = 5 },
    }
  },
  {
    .name = "missing_source_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
    },
    .builds = {
      { .expect_err = SPN_ERR_DAG_MISSING_INPUT, .expect_diag_path = "S" },
    }
  },
  {
    .name = "missing_output_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .skips_output = true },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_err = SPN_ERR_DAG_MISSING_OUTPUT, .expect_diag_path = "X", .expect_runs = 1 },
    }
  },
  {
    .name = "cycle_fails",
    .actions = {
      { .identity = "I", .inputs = { "Y" }, .output = "X" },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .expect_err = SPN_ERR_DAG_STALLED },
    }
  },
  {
    .name = "failing_action_stops_build",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .fails = true },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_err = SPN_ERR_DAG_ACTION },
    }
  },
  {
    .name = "uncacheable_stable_output_downstream_hits",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .writes = "C", .kind = SPN_DAG_ACTION_UNCACHEABLE },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" } }, .expect_runs = 3 },
    }
  },
  {
    .name = "uncacheable_changed_output_reruns_downstream",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_UNCACHEABLE },
      { .identity = "J", .inputs = { "X" }, .output = "Y" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" } }, .expect_runs = 4 },
    }
  },
  {
    .name = "discovered_generated_header_waits_for_producer",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "H" },
      { .identity = "J", .inputs = { "M" }, .output = "O", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "M", "B" } }, .observes = { { SPN_DAG_OBS_FILE, "H" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" }, { "M", "B" } }, .observes = { { SPN_DAG_OBS_FILE, "H" } }, .expect_runs = 2 },
      { .sources = { { "S", "C" }, { "M", "B" } }, .observes = { { SPN_DAG_OBS_FILE, "H" } }, .expect_runs = 4 },
    }
  },
  {
    .name = "discovered_tree_member_waits_for_producer",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "D", .tree = true },
      { .identity = "J", .inputs = { "M" }, .output = "O", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "M", "B" } }, .observes = { { SPN_DAG_OBS_FILE, "D/H" } }, .expect_runs = 2 },
      { .sources = { { "S", "A" }, { "M", "B" } }, .observes = { { SPN_DAG_OBS_FILE, "D/H" } }, .remove_dirs = { "D" }, .expect_runs = 2 },
    }
  },
  {
    .name = "discovered_source_header_no_deferral",
    .actions = {
      { .identity = "I", .inputs = { "M" }, .output = "O", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "M", "A" }, { "H", "B" } }, .observes = { { SPN_DAG_OBS_FILE, "H" } }, .expect_runs = 1 },
      { .sources = { { "M", "A" }, { "H", "B" } }, .observes = { { SPN_DAG_OBS_FILE, "H" } }, .expect_runs = 1 },
      { .sources = { { "M", "A" }, { "H", "C" } }, .observes = { { SPN_DAG_OBS_FILE, "H" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "absent_staged_file_passes",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X" },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .staged = { { "G" } }, .expect_runs = 1 },
    }
  },
  {
    .name = "staged_file_read_fails_unrecorded",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "G", "B" } }, .observes = { { SPN_DAG_OBS_FILE, "G" } }, .staged = { { "G" } }, .expect_err = SPN_ERR_STAGE_OBSERVED, .expect_diag_path = "G", .expect_runs = 1 },
      { .observes = { { SPN_DAG_OBS_FILE, "G" } }, .expect_runs = 2 },
    }
  },
  {
    .name = "staged_file_probe_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .observes = { { SPN_DAG_OBS_ABSENT, "G" } }, .staged = { { "G" } }, .expect_err = SPN_ERR_STAGE_OBSERVED, .expect_diag_path = "G", .expect_runs = 1 },
    }
  },
  {
    .name = "staged_file_missing_parent_probe_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .observes = { { SPN_DAG_OBS_ABSENT, "D" } }, .staged = { { "D/G" } }, .expect_err = SPN_ERR_STAGE_OBSERVED, .expect_diag_path = "D", .expect_runs = 1 },
    }
  },
  {
    .name = "staged_dir_member_read_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .observes = { { SPN_DAG_OBS_FILE, "D/G" } }, .staged = { { "D", .tree = true } }, .expect_err = SPN_ERR_STAGE_OBSERVED, .expect_diag_path = "D/G", .expect_runs = 1 },
    }
  },
  {
    .name = "staged_dir_member_probe_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .observes = { { SPN_DAG_OBS_ABSENT, "D/G" } }, .staged = { { "D", .tree = true } }, .expect_err = SPN_ERR_STAGE_OBSERVED, .expect_diag_path = "D/G", .expect_runs = 1 },
    }
  },
  {
    .name = "staged_dir_member_listing_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .observes = { { SPN_DAG_OBS_ENUMERATION, "D/E" } }, .staged = { { "D", .tree = true } }, .expect_err = SPN_ERR_STAGE_OBSERVED, .expect_diag_path = "D/E", .expect_runs = 1 },
    }
  },
  {
    .name = "staged_parent_listing_admitted_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .observes = { { SPN_DAG_OBS_ENUMERATION, "D", "G" } }, .staged = { { "D/G" } }, .expect_err = SPN_ERR_STAGE_OBSERVED, .expect_diag_path = "D", .expect_runs = 1 },
    }
  },
  {
    .name = "staged_parent_listing_rejected_passes",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .observes = { { SPN_DAG_OBS_ENUMERATION, "D", "H" } }, .staged = { { "D/G" } }, .expect_runs = 1 },
    }
  },
  {
    .name = "staged_parent_listing_bad_filter_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .observes = { { SPN_DAG_OBS_ENUMERATION, "D", "[" } }, .staged = { { "D/G" } }, .expect_err = SPN_ERR_DAG_GLOB, .expect_diag_path = "D", .expect_runs = 1 },
    }
  },
  {
    .name = "staged_grandparent_listing_passes",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" } }, .observes = { { SPN_DAG_OBS_ENUMERATION, "D" } }, .staged = { { "D/E/G" } }, .expect_runs = 1 },
    }
  },
  {
    .name = "staged_read_cache_hit_fails",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "G", "B" } }, .observes = { { SPN_DAG_OBS_FILE, "G" } }, .expect_runs = 1 },
      { .staged = { { "G" } }, .expect_err = SPN_ERR_STAGE_OBSERVED, .expect_diag_path = "G", .expect_runs = 1 },
    }
  },
  {
    .name = "staged_read_dropped_on_rerun_passes",
    .actions = {
      { .identity = "I", .inputs = { "S" }, .output = "X", .kind = SPN_DAG_ACTION_DISCOVERED },
    },
    .builds = {
      { .sources = { { "S", "A" }, { "M", "B" }, { "G", "C" } }, .observes = { { SPN_DAG_OBS_FILE, "M" }, { SPN_DAG_OBS_FILE, "G" } }, .expect_runs = 1 },
      { .sources = { { "M", "D" } }, .observes = { { SPN_DAG_OBS_FILE, "M" } }, .staged = { { "G" } }, .expect_runs = 2 },
    }
  },
};

static spn_err_t execute_action(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  ctx_t* ctx = (ctx_t*)user_data;
  if (ctx->spec->fails) {
    return SPN_ERR_DAG_ACTION;
  }

  if (action->kind == SPN_DAG_ACTION_DISCOVERED) {
    u32 observes = 0;
    sp_carr_detect_len(ctx->build->observes, observes, ctx->build->observes[observes].path);
    sp_for(it, observes) {
      const obs_t* o = &ctx->build->observes[it];
      spn_dag_observe(obs, (spn_dag_obs_t) {
        .kind = o->kind,
        .path = dag_test_env_rooted(ctx->env, sp_cstr_as_str(o->path)),
        .filter = sp_cstr_as_str(o->filter),
      });
    }
  }

  if (ctx->spec->skips_output) {
    ctx->env->runs++;
    return SPN_OK;
  }
  sp_da_for(action->consumes, it) {
    spn_dag_artifact_t* in = spn_dag_find_artifact(ctx->g, action->consumes[it]);
    if (in->kind == SPN_DAG_ARTIFACT_KIND_FILE && !sp_fs_exists_at(dag_test_at(ctx->env, in->materialized))) {
      return SPN_ERR_DAG_ACTION;
    }
  }
  ctx->env->runs++;
  spn_dag_artifact_t* out = spn_dag_find_artifact(ctx->g, action->produces[0]);
  sp_str_t content = ctx->spec->writes
    ? sp_str_view(ctx->spec->writes)
    : sp_fmt(ctx->env->mem, "{}", sp_fmt_uint(ctx->env->runs)).value;
  if (out->kind == SPN_DAG_ARTIFACT_KIND_TREE) {
    spn_path_t inside = spn_path_join(ctx->env->mem, outputs[0], sp_str_lit("H"));
    return sp_fs_create_file_str_at(dag_test_at(ctx->env, inside), sp_str_lit("T")) ? SPN_ERR_DAG_ACTION : SPN_OK;
  }
  return sp_fs_create_file_str_at(dag_test_at(ctx->env, outputs[0]), content) ? SPN_ERR_DAG_ACTION : SPN_OK;
}

sp_test_each(dag_run, builds, test_t, tests) {
  dag_test_env_t env;
  dag_test_env_init(&env, t, (dag_test_env_config_t) {
    .store = SPN_DAG_STORE_MEM
  });

  sp_carr_for(it->builds, b) {
    const build_t* build = &it->builds[b];
    if (!build->expect_runs && !build->expect_err) {
      break;
    }

    spn_dag_file_cache_invalidate_all(&env.files);
    sp_carr_for(build->sources, si) {
      if (!build->sources[si].path) {
        break;
      }
      sp_fs_create_file_str_at(dag_test_env_path(&env, sp_str_view(build->sources[si].path)), sp_str_view(build->sources[si].content));
    }
    sp_carr_for(build->remove_dirs, si) {
      if (!build->remove_dirs[si]) {
        break;
      }
      sp_fs_remove_dir_at(dag_test_env_path(&env, sp_str_view(build->remove_dirs[si])));
    }

    spn_dag_t* g = dag_test_env_graph(&env);
    sp_carr_for(it->actions, ai) {
      const action_t* spec = &it->actions[ai];
      if (!spec->identity) {
        break;
      }

      ctx_t* ctx = sp_alloc_type(env.mem, ctx_t);
      ctx->env = &env;
      ctx->g = g;
      ctx->spec = spec;
      ctx->build = build;

      spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .kind = spec->kind,
        .identity = dag_test_digest(spec->identity),
        .execute = execute_action,
        .user_data = ctx
      });
      sp_carr_for(spec->inputs, ii) {
        if (!spec->inputs[ii]) {
          break;
        }
        spn_dag_action_add_input(g, action, spn_dag_add_file(g, dag_test_env_rooted(&env, sp_str_view(spec->inputs[ii]))));
      }
      spn_path_t output = dag_test_env_rooted(&env, sp_str_view(spec->output));
      spn_dag_id_t out_id = spec->tree ? spn_dag_add_tree(g, output) : spn_dag_add_file(g, output);
      sp_must_eq(t, SPN_OK, spn_dag_action_add_output(g, action, out_id));
    }

    u32 staged = 0;
    sp_carr_detect_len(build->staged, staged, build->staged[staged].path);
    sp_for(st, staged) {
      const claim_t* claim = &build->staged[st];
      spn_dag_add_staged(g, dag_test_env_rooted(&env, sp_cstr_as_str(claim->path)), claim->tree ? SPN_DAG_ARTIFACT_KIND_TREE : SPN_DAG_ARTIFACT_KIND_FILE);
    }

    spn_err_t err = dag_test_env_run(&env, g);
    sp_expect_eq(t, build->expect_err, err);
    sp_expect_eq(t, build->expect_err, env.run.diag.err);
    if (build->expect_diag_path) {
      sp_expect_str_eq(t, env.run.diag.path, spn_path_str(&env.roots, env.mem, dag_test_env_rooted(&env, sp_str_view(build->expect_diag_path))));
    }
    sp_expect_eq(t, build->expect_runs, env.runs);
  }

  return SP_OK;
}
