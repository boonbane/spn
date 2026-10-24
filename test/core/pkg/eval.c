#include "spn_test.h"

#include "intern/intern.h"
#include "paths/paths.h"
#include "manifest.h"
#include "pkg/pkg.h"
#include "when/when.h"

#define EVAL_TEST_MAX 4

typedef struct {
  const c8* key;
  const c8* value;
  bool negated;
} clause_t;

typedef struct {
  const c8* value;
  clause_t when [2];
} gated_t;

typedef struct {
  gated_t values [EVAL_TEST_MAX];
} list_t;

typedef struct {
  const c8* lib_source [EVAL_TEST_MAX];
  const c8* source [EVAL_TEST_MAX];
  const c8* script_source [EVAL_TEST_MAX];
  const c8* test_source [EVAL_TEST_MAX];
  const c8* build_source [EVAL_TEST_MAX];
  const c8* include [EVAL_TEST_MAX];
  const c8* define [EVAL_TEST_MAX];
  const c8* flags [EVAL_TEST_MAX];
  const c8* link_flags [EVAL_TEST_MAX];
  const c8* linker_script [EVAL_TEST_MAX];
  const c8* sys_target [EVAL_TEST_MAX];
  const c8* deps [EVAL_TEST_MAX];
  const c8* sys [EVAL_TEST_MAX];
  const c8* pkg_define [EVAL_TEST_MAX];
  const c8* frameworks [EVAL_TEST_MAX];
  const c8* pkg_frameworks [EVAL_TEST_MAX];
} lists_expect_t;

typedef struct {
  const c8* name;
  spn_when_facts_t facts;
  list_t lib_source;
  list_t source;
  list_t script_source;
  list_t test_source;
  list_t build_source;
  list_t include;
  list_t define;
  list_t flags;
  list_t link_flags;
  list_t linker_script;
  list_t sys_target;
  list_t deps;
  list_t sys;
  list_t pkg_define;
  list_t frameworks;
  list_t pkg_frameworks;
  lists_expect_t expect;
} lists_test_t;

static spn_when_t make_when(sp_mem_t mem, const clause_t* clauses, u64 count) {
  spn_when_t when = sp_zero;
  sp_for(it, count) {
    if (!clauses[it].key) {
      break;
    }
    si_da_push(mem, when.clauses, ((spn_when_clause_t) {
      .key = sp_cstr_as_str(clauses[it].key),
      .negated = clauses[it].negated,
      .value = spn_option_value_str(sp_cstr_as_str(clauses[it].value)),
    }));
  }
  return when;
}

static spn_gated_list_t make_list(sp_mem_t mem, list_t test) {
  spn_gated_list_t gated = SP_NULLPTR;
  sp_carr_for(test.values, it) {
    gated_t* value = &test.values[it];
    if (!value->value) {
      break;
    }
    si_da_push(mem, gated, ((spn_gated_str_t) {
      .value = sp_cstr_as_str(value->value),
      .when = make_when(mem, value->when, sp_carr_len(value->when)),
    }));
  }
  return gated;
}

static spn_gated_path_list_t make_paths(sp_mem_t mem, list_t test) {
  spn_gated_path_list_t gated = SP_NULLPTR;
  sp_carr_for(test.values, it) {
    gated_t* value = &test.values[it];
    if (!value->value) {
      break;
    }
    si_da_push(mem, gated, ((spn_gated_path_t) {
      .path = sp_cstr_as_str(value->value),
      .tree = SPN_TREE_SOURCE,
      .when = make_when(mem, value->when, sp_carr_len(value->when)),
    }));
  }
  return gated;
}

static si_da(spn_gated_source_t) make_sources(sp_mem_t mem, list_t test) {
  si_da(spn_gated_source_t) gated = SP_NULLPTR;
  sp_carr_for(test.values, it) {
    gated_t* value = &test.values[it];
    if (!value->value) {
      break;
    }
    si_da_push(mem, gated, ((spn_gated_source_t) {
      .path = sp_cstr_as_str(value->value),
      .tree = SPN_TREE_SOURCE,
      .when = make_when(mem, value->when, sp_carr_len(value->when)),
    }));
  }
  return gated;
}

static spn_when_env_t make_env(sp_mem_t mem, spn_when_facts_t facts) {
  spn_when_env_t env = sp_zero;
  spn_when_env_init(mem, &env);
  spn_when_env_set_facts(&env, facts);
  return env;
}

static si_pkg_t eval(sp_mem_t mem, const si_pkg_info_t* manifest, spn_when_env_t* env) {
  spn_path_roots_t roots = sp_zero;
  spn_tree_roots_t trees = { .source = spn_path_from_root(SPN_PATH_ROOT_PROJECT) };
  return spn_pkg_eval(mem, manifest, &roots, trees, env);
}

static sp_err_t expect_list(sp_test_t* t, si_da(sp_str_t) actual, const c8* const* expected) {
  sp_for(et, EVAL_TEST_MAX) {
    if (!expected[et]) {
      sp_must_eq(t, si_da_size(actual), et);
      return SP_OK;
    }
    sp_test_kv_c(t, "value", expected[et]);
    sp_must(t, et < si_da_size(actual));
    sp_expect_str_eq_c(t, actual[et], expected[et]);
  }
  sp_must_eq(t, si_da_size(actual), EVAL_TEST_MAX);
  return SP_OK;
}

static sp_err_t expect_paths(sp_test_t* t, si_da(spn_path_t) actual, const c8* const* expected) {
  sp_for(et, EVAL_TEST_MAX) {
    if (!expected[et]) {
      sp_must_eq(t, si_da_size(actual), et);
      return SP_OK;
    }
    sp_test_kv_c(t, "value", expected[et]);
    sp_must(t, et < si_da_size(actual));
    sp_expect_str_eq_c(t, actual[et].sub, expected[et]);
  }
  sp_must_eq(t, si_da_size(actual), EVAL_TEST_MAX);
  return SP_OK;
}

static sp_err_t expect_sources(sp_test_t* t, si_da(spn_source_t) actual, const c8* const* expected) {
  sp_for(et, EVAL_TEST_MAX) {
    if (!expected[et]) {
      sp_must_eq(t, si_da_size(actual), et);
      return SP_OK;
    }
    sp_test_kv_c(t, "value", expected[et]);
    sp_must(t, et < si_da_size(actual));
    sp_expect_str_eq_c(t, actual[et].path.sub, expected[et]);
  }
  sp_must_eq(t, si_da_size(actual), EVAL_TEST_MAX);
  return SP_OK;
}

static const lists_test_t lists_tests [] = {
  {
    .name = "lib_source",
    .facts = { .os = SPN_OS_LINUX },
    .lib_source = { .values = { { "A" }, { "B", { { "os", "windows" } } }, { "C", { { "os", "linux" } } } } },
    .expect = { .lib_source = { "A", "C" } },
  },
  {
    .name = "exe_source",
    .facts = { .os = SPN_OS_LINUX },
    .source = { .values = { { "A" }, { "B", { { "os", "windows" } } }, { "C", { { "os", "linux" } } } } },
    .expect = { .source = { "A", "C" } },
  },
  {
    .name = "script_source",
    .facts = { .os = SPN_OS_LINUX },
    .script_source = { .values = { { "A" }, { "B", { { "os", "windows" } } }, { "C", { { "os", "linux" } } } } },
    .expect = { .script_source = { "A", "C" } },
  },
  {
    .name = "test_source",
    .facts = { .os = SPN_OS_LINUX },
    .test_source = { .values = { { "A" }, { "B", { { "os", "windows" } } }, { "C", { { "os", "linux" } } } } },
    .expect = { .test_source = { "A", "C" } },
  },
  {
    .name = "metaprogram_source",
    .facts = { .os = SPN_OS_WASI },
    .build_source = { .values = { { "A" }, { "B", { { "os", "linux" } } }, { "C", { { "os", "wasi" } } } } },
    .expect = { .build_source = { "A", "C" } },
  },
  {
    .name = "include",
    .facts = { .os = SPN_OS_LINUX },
    .include = { .values = { { "A", { { "os", "linux" } } } } },
    .expect = { .include = { "A" } },
  },
  {
    .name = "define",
    .facts = { .os = SPN_OS_LINUX },
    .define = { .values = { { "SPUM", { { "os", "linux" } } } } },
    .expect = { .define = { "SPUM" } },
  },
  {
    .name = "flags",
    .facts = { .os = SPN_OS_LINUX },
    .flags = { .values = { { "A", { { "os", "linux" } } } } },
    .expect = { .flags = { "A" } },
  },
  {
    .name = "link_flags",
    .facts = { .os = SPN_OS_LINUX },
    .link_flags = { .values = { { "A", { { "os", "linux" } } } } },
    .expect = { .link_flags = { "A" } },
  },
  {
    .name = "linker_script",
    .facts = { .os = SPN_OS_LINUX },
    .linker_script = { .values = { { "A", { { "os", "linux" } } } } },
    .expect = { .linker_script = { "A" } },
  },
  {
    .name = "target_system_deps",
    .facts = { .os = SPN_OS_LINUX },
    .sys_target = { .values = { { "A", { { "os", "linux" } } } } },
    .expect = { .sys_target = { "A" } },
  },
  {
    .name = "target_deps",
    .facts = { .os = SPN_OS_LINUX },
    .deps = { .values = { { "A", { { "os", "linux" } } } } },
    .expect = { .deps = { "A" } },
  },
  {
    .name = "package_system_deps",
    .facts = { .os = SPN_OS_LINUX },
    .sys = { .values = { { "A", { { "os", "linux" } } } } },
    .expect = { .sys = { "A" } },
  },
  {
    .name = "package_define",
    .facts = { .os = SPN_OS_LINUX },
    .pkg_define = { .values = { { "A", { { "os", "linux" } } }, { "B", { { "os", "windows" } } } } },
    .expect = { .pkg_define = { "A" } },
  },
  {
    .name = "target_frameworks",
    .facts = { .os = SPN_OS_MACOS },
    .frameworks = { .values = { { "A", { { "os", "macos" } } }, { "B", { { "os", "linux" } } } } },
    .expect = { .frameworks = { "A" } },
  },
  {
    .name = "package_frameworks",
    .facts = { .os = SPN_OS_MACOS },
    .pkg_frameworks = { .values = { { "A", { { "os", "macos" } } }, { "B", { { "os", "linux" } } } } },
    .expect = { .pkg_frameworks = { "A" } },
  },
};

sp_test_each(pkg_eval, lists, lists_test_t, lists_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);

  si_pkg_info_t manifest = test_manifest_make(sp_str_lit("P"));
  si_target_info_t* lib = SP_NULLPTR;
  si_target_info_t* exe = SP_NULLPTR;
  si_target_info_t* script = SP_NULLPTR;
  si_target_info_t* unit_test = SP_NULLPTR;
  si_target_info_t* build = SP_NULLPTR;
  lib = test_manifest_target(mem, &manifest, sp_str_lit("A"), SPN_TARGET_KIND_LIB);
  exe = test_manifest_target(mem, &manifest, sp_str_lit("main"), SPN_TARGET_KIND_EXE);
  script = test_manifest_target(mem, &manifest, sp_str_lit("B"), SPN_TARGET_KIND_SCRIPT);
  unit_test = test_manifest_target(mem, &manifest, sp_str_lit("C"), SPN_TARGET_KIND_TEST);
  build = test_manifest_target(mem, &manifest, sp_str_lit("build"), SPN_TARGET_KIND_BUILD_METAPROGRAM);

  lib->source = make_sources(mem, it->lib_source);
  exe->source = make_sources(mem, it->source);
  script->source = make_sources(mem, it->script_source);
  unit_test->source = make_sources(mem, it->test_source);
  build->source = make_sources(mem, it->build_source);
  exe->include = make_paths(mem, it->include);
  exe->linker_script = make_paths(mem, it->linker_script);
  exe->define = make_list(mem, it->define);
  exe->flags = make_list(mem, it->flags);
  exe->link_flags = make_list(mem, it->link_flags);
  exe->system_deps = make_list(mem, it->sys_target);
  exe->deps = make_list(mem, it->deps);
  exe->macos.frameworks = make_list(mem, it->frameworks);
  manifest.system_deps = make_list(mem, it->sys);
  manifest.define = make_list(mem, it->pkg_define);
  manifest.macos.frameworks = make_list(mem, it->pkg_frameworks);

  spn_when_env_t env = make_env(mem, it->facts);
  si_pkg_t pkg = eval(mem, &manifest, &env);
  si_target_t* lib_eval = spn_pkg_get_target(&pkg, sp_str_lit("A"), SPN_TARGET_KIND_LIB);
  si_target_t* exe_eval = spn_pkg_get_target(&pkg, sp_str_lit("main"), SPN_TARGET_KIND_EXE);
  si_target_t* script_eval = spn_pkg_get_target(&pkg, sp_str_lit("B"), SPN_TARGET_KIND_SCRIPT);
  si_target_t* test_eval = spn_pkg_get_target(&pkg, sp_str_lit("C"), SPN_TARGET_KIND_TEST);
  si_target_t* build_eval = spn_pkg_get_target(&pkg, sp_str_lit("build"), SPN_TARGET_KIND_BUILD_METAPROGRAM);
  sp_must(t, lib_eval && exe_eval && script_eval && test_eval && build_eval);
  sp_expect(t, exe_eval->info == exe);

  sp_try(expect_sources(t, lib_eval->source, it->expect.lib_source));
  sp_try(expect_sources(t, exe_eval->source, it->expect.source));
  sp_try(expect_sources(t, script_eval->source, it->expect.script_source));
  sp_try(expect_sources(t, test_eval->source, it->expect.test_source));
  sp_try(expect_sources(t, build_eval->source, it->expect.build_source));
  sp_try(expect_paths(t, exe_eval->include, it->expect.include));
  sp_try(expect_paths(t, exe_eval->linker_script, it->expect.linker_script));
  sp_try(expect_list(t, exe_eval->define, it->expect.define));
  sp_try(expect_list(t, exe_eval->flags, it->expect.flags));
  sp_try(expect_list(t, exe_eval->link_flags, it->expect.link_flags));
  sp_try(expect_list(t, exe_eval->system_deps, it->expect.sys_target));
  sp_try(expect_list(t, exe_eval->deps, it->expect.deps));
  sp_try(expect_list(t, exe_eval->macos.frameworks, it->expect.frameworks));
  sp_try(expect_list(t, pkg.system_deps, it->expect.sys));
  sp_try(expect_list(t, pkg.define, it->expect.pkg_define));
  sp_try(expect_list(t, pkg.macos.frameworks, it->expect.pkg_frameworks));
  return SP_OK;
}

typedef struct {
  const c8* name;
  const c8* define;
  bool public;
} option_t;

typedef struct {
  const c8* name;
  const c8* str;
  bool b;
  bool is_bool;
} env_value_t;

typedef struct {
  const c8* define [EVAL_TEST_MAX];
  const c8* public_define [EVAL_TEST_MAX];
} options_expect_t;

typedef struct {
  const c8* name;
  const c8* define [EVAL_TEST_MAX];
  option_t options [EVAL_TEST_MAX];
  env_value_t env [EVAL_TEST_MAX];
  options_expect_t expect;
} options_test_t;

static const options_test_t options_tests [] = {
  {
    .name = "private_define",
    .define = { "A" },
    .options = { { .name = "A", .define = "SPUM" } },
    .env = { { .name = "A", .is_bool = true, .b = true } },
    .expect = { .define = { "A", "SPUM" } },
  },
  {
    .name = "public_define",
    .options = { { .name = "A", .define = "SPUM", .public = true } },
    .env = { { .name = "A", .is_bool = true, .b = true } },
    .expect = { .define = { "SPUM" }, .public_define = { "SPUM" } },
  },
  {
    .name = "multiple_options",
    .options = { { .name = "A", .define = "A" }, { .name = "B", .define = "SPUM" } },
    .env = { { .name = "A", .is_bool = true }, { .name = "B", .is_bool = true, .b = true } },
    .expect = { .define = { "SPUM" } },
  },
  {
    .name = "empty_define",
    .options = { { .name = "A", .define = "" } },
    .env = { { .name = "A", .is_bool = true, .b = true } },
  },
  {
    .name = "missing_option_value",
    .options = { { .name = "A", .define = "SPUM" } },
  },
  {
    .name = "non_bool_option_value",
    .options = { { .name = "A", .define = "SPUM" } },
    .env = { { .name = "A", .str = "A" } },
  },
  {
    .name = "false_option_value",
    .options = { { .name = "A", .define = "SPUM" } },
    .env = { { .name = "A", .is_bool = true } },
  },
};

sp_test_each(pkg_eval, option_defines, options_test_t, options_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  si_pkg_info_t manifest = sp_zero;

  sp_carr_for(it->define, dt) {
    if (!it->define[dt]) {
      break;
    }
    si_da_push(mem, manifest.define, ((spn_gated_str_t) { .value = sp_cstr_as_str(it->define[dt]) }));
  }
  sp_carr_for(it->options, ot) {
    if (!it->options[ot].name) {
      break;
    }
    sp_intern_str_t name = spn_intern(sp_cstr_as_str(it->options[ot].name));
    spn_option_info_t option = {
      .id = name.id,
      .name = name,
      .public = it->options[ot].public,
      .define = sp_cstr_as_str(it->options[ot].define),
    };
    si_om_insert(mem, manifest.options, option.id, option);
  }

  spn_when_env_t env = sp_zero;
  spn_when_env_init(mem, &env);
  sp_carr_for(it->env, et) {
    if (!it->env[et].name) {
      break;
    }
    spn_option_value_t value = it->env[et].is_bool
      ? spn_option_value_bool(it->env[et].b)
      : spn_option_value_str(sp_cstr_as_str(it->env[et].str));
    spn_when_env_set(&env, sp_cstr_as_str(it->env[et].name), value);
  }

  si_pkg_t pkg = eval(mem, &manifest, &env);
  sp_try(expect_list(t, pkg.define, it->expect.define));
  return expect_list(t, pkg.public_define, it->expect.public_define);
}

typedef struct {
  const c8* pattern;
  spn_publish_root_t root;
  spn_source_kind_t kind;
  clause_t when [2];
} publish_t;

typedef struct {
  const c8* pattern;
  spn_publish_root_t root;
  spn_source_kind_t kind;
} publish_expect_t;

typedef struct {
  const c8* name;
  spn_when_facts_t facts;
  publish_t entries [EVAL_TEST_MAX];
  publish_expect_t expect [EVAL_TEST_MAX];
} publish_test_t;

static const publish_test_t publish_tests [] = {
  {
    .name = "publish_entries",
    .facts = { .os = SPN_OS_LINUX },
    .entries = {
      { .pattern = "a.h", .when = { { "os", "linux" } } },
      { .pattern = "b.h", .when = { { "os", "windows" } } },
      { .pattern = "d/*", .root = SPN_PUBLISH_ROOT_SHARE, .kind = SPN_SOURCE_GLOB },
    },
    .expect = {
      { "a.h" },
      { "d/*", SPN_PUBLISH_ROOT_SHARE, SPN_SOURCE_GLOB },
    },
  },
};

sp_test_each(pkg_eval, publish, publish_test_t, publish_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  si_pkg_info_t manifest = sp_zero;
  sp_carr_for(it->entries, et) {
    if (!it->entries[et].pattern) {
      break;
    }
    si_da_push(mem, manifest.publish, ((spn_gated_publish_t) {
      .root = it->entries[et].root,
      .source = {
        .kind = it->entries[et].kind,
        .path = sp_cstr_as_str(it->entries[et].pattern),
        .tree = SPN_TREE_SOURCE,
        .when = make_when(mem, it->entries[et].when, sp_carr_len(it->entries[et].when)),
      },
    }));
  }

  spn_when_env_t env = make_env(mem, it->facts);
  si_pkg_t pkg = eval(mem, &manifest, &env);

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected].pattern);
  sp_must_eq(t, expected, (u32)si_da_size(pkg.publish));
  sp_for(et, expected) {
    sp_expect_str_eq_c(t, pkg.publish[et].source.path.sub, it->expect[et].pattern);
    sp_expect_eq(t, (u32)it->expect[et].root, (u32)pkg.publish[et].root);
    sp_expect_eq(t, (u32)it->expect[et].kind, (u32)pkg.publish[et].source.kind);
  }
  return SP_OK;
}

typedef struct {
  const c8* dest;
  clause_t when [2];
} embed_t;

typedef struct {
  const c8* name;
  spn_when_facts_t facts;
  embed_t embeds [EVAL_TEST_MAX];
  const c8* expect [EVAL_TEST_MAX];
} embed_test_t;

static const embed_test_t embed_tests [] = {
  {
    .name = "target_embeds",
    .facts = { .os = SPN_OS_LINUX },
    .embeds = {
      { .dest = "A", .when = { { "os", "linux" } } },
      { .dest = "B", .when = { { "os", "windows" } } },
      { .dest = "C" },
    },
    .expect = { "A", "C" },
  },
};

sp_test_each(pkg_eval, target_embeds, embed_test_t, embed_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  si_pkg_info_t manifest = test_manifest_make(sp_str_lit("P"));
  si_target_info_t* exe = SP_NULLPTR;
  exe = test_manifest_target(mem, &manifest, sp_str_lit("main"), SPN_TARGET_KIND_EXE);
  sp_carr_for(it->embeds, et) {
    if (!it->embeds[et].dest) {
      break;
    }
    si_da_push(mem, exe->embed, ((spn_gated_embed_t) {
      .kind = SPN_EMBED_DIR,
      .path = sp_cstr_as_str(it->embeds[et].dest),
      .tree = SPN_TREE_SOURCE,
      .dest = sp_cstr_as_str(it->embeds[et].dest),
      .types = { .data = sp_str_lit("u8"), .size = sp_str_lit("u64") },
      .when = make_when(mem, it->embeds[et].when, sp_carr_len(it->embeds[et].when)),
    }));
  }

  spn_when_env_t env = make_env(mem, it->facts);
  si_pkg_t pkg = eval(mem, &manifest, &env);
  si_target_t* target = spn_pkg_get_target(&pkg, sp_str_lit("main"), SPN_TARGET_KIND_EXE);
  sp_must(t, target);

  u32 expected = 0;
  sp_carr_detect_len(it->expect, expected, it->expect[expected]);
  sp_must_eq(t, expected, (u32)si_da_size(target->embed));
  sp_for(et, expected) {
    sp_expect_str_eq_c(t, target->embed[et].dest, it->expect[et]);
    sp_expect_str_eq_c(t, target->embed[et].path.sub, it->expect[et]);
    sp_expect_eq(t, (u32)SPN_EMBED_DIR, (u32)target->embed[et].kind);
    sp_expect_str_eq_c(t, target->embed[et].types.data, "u8");
    sp_expect_str_eq_c(t, target->embed[et].types.size, "u64");
  }
  return SP_OK;
}

typedef struct {
  spn_target_kind_t kind;
  const c8* source;
  clause_t when;
} scripts_target_t;

typedef struct {
  const c8* name;
  spn_when_facts_t facts;
  scripts_target_t targets [2];
  bool expect;
} scripts_test_t;

static const scripts_test_t scripts_tests [] = {
  { .name = "no_targets" },
  { .name = "configure_source",     .targets = { { SPN_TARGET_KIND_CONFIGURE_METAPROGRAM, "A" } }, .expect = true },
  { .name = "build_source",         .targets = { { SPN_TARGET_KIND_BUILD_METAPROGRAM, "A" } }, .expect = true },
  { .name = "lib_source_only",      .targets = { { SPN_TARGET_KIND_LIB, "A" } } },
  { .name = "script_source_gated_out", .facts = { .os = SPN_OS_WASI }, .targets = { { SPN_TARGET_KIND_BUILD_METAPROGRAM, "A", { "os", "linux" } } } },
};

sp_test_each(pkg_eval, has_scripts, scripts_test_t, scripts_tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  si_pkg_info_t manifest = test_manifest_make(sp_str_lit("P"));
  sp_carr_for(it->targets, tt) {
    if (!it->targets[tt].source) {
      break;
    }
    si_target_info_t* decl = SP_NULLPTR;
    decl = test_manifest_target(mem, &manifest, sp_str_lit("T"), it->targets[tt].kind);
    si_da_push(mem, decl->source, ((spn_gated_source_t) {
      .path = sp_cstr_as_str(it->targets[tt].source),
      .tree = SPN_TREE_SOURCE,
      .when = make_when(mem, &it->targets[tt].when, 1),
    }));
  }

  spn_when_env_t env = make_env(mem, it->facts);
  si_pkg_t pkg = eval(mem, &manifest, &env);
  sp_expect_eq(t, it->expect, spn_pkg_has_scripts(&pkg));
  return SP_OK;
}
