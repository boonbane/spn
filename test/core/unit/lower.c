#include "unit.h"

#include "dag/dag.h"
#include "graph/dag.h"
#include "graph/nodes/nodes.h"
#include "op/types.h"
#include "paths/paths.h"

#define LOWER_TEST_MAX_ACTIONS 10
#define LOWER_TEST_MAX_LABELS 4

typedef enum {
  NODE_NONE,
  NODE_COMPILE,
  NODE_ARCHIVE,
  NODE_LINK,
  NODE_EXPORTS,
  NODE_EMBED,
  NODE_PUBLISH,
  NODE_PUBLISH_FILE,
  NODE_USER,
  NODE_WARM,
  NODE_LIBC,
  NODE_RSP,
} node_t;

typedef struct {
  node_t kind;
  const c8* consumes [LOWER_TEST_MAX_LABELS];
  const c8* orders [LOWER_TEST_MAX_LABELS];
  const c8* produces [LOWER_TEST_MAX_LABELS];
} action_t;

typedef struct {
  const c8* name;
  graph_t graph;
  graph_root_t roots [UNIT_TEST_MAX_ROOTS];
  action_t actions [LOWER_TEST_MAX_ACTIONS];
} test_t;

static const test_t tests [] = {
  {
    .name = "exe_with_static_dep",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .targets = { { "B", static_only, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_COMPILE, .consumes = { "main.c" }, .produces = { "main.c.o" } },
      { NODE_COMPILE, .consumes = { "b.c" }, .produces = { "b.c.o" } },
      { NODE_ARCHIVE, .consumes = { "b.c.o" }, .produces = { "libB.a" } },
      { NODE_LINK, .consumes = { "main.c.o", "libB.a" }, .produces = { "A" } },
      { NODE_PUBLISH_FILE, .consumes = { "libB.a" }, .produces = { "build/debug/store/B/lib/libB.a" } },
      { NODE_PUBLISH_FILE, .consumes = { "A" }, .produces = { "build/debug/store/P/bin/A" } },
    },
  },
  {
    .name = "shared_dep_links_through_its_exports",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_COMPILE, .consumes = { "main.c" }, .produces = { "main.c.o" } },
      { NODE_COMPILE, .consumes = { "b.c" }, .produces = { "b.c.o" } },
      { NODE_EXPORTS, .consumes = { "b.c.o" } },
      { NODE_LINK, .consumes = { "b.c.o", "@exports" }, .produces = { "libB.so" } },
      { NODE_LINK, .consumes = { "main.c.o", "libB.so" }, .produces = { "A" } },
      { NODE_PUBLISH_FILE, .consumes = { "libB.so" }, .produces = { "build/debug/store/B/lib/libB.so" } },
      { NODE_PUBLISH_FILE, .consumes = { "A" }, .produces = { "build/debug/store/P/bin/A" } },
    },
  },
  {
    .name = "object_lib_publishes_each_object",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .targets = { { "B", object_only, .source = { "b.c", "c.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_COMPILE, .consumes = { "main.c" }, .produces = { "main.c.o" } },
      { NODE_COMPILE, .consumes = { "b.c" }, .produces = { "b.c.o" } },
      { NODE_COMPILE, .consumes = { "c.c" }, .produces = { "c.c.o" } },
      { NODE_PUBLISH_FILE, .consumes = { "b.c.o" }, .produces = { "build/debug/store/B/lib/manifest/b.c.o" } },
      { NODE_PUBLISH_FILE, .consumes = { "c.c.o" }, .produces = { "build/debug/store/B/lib/manifest/c.c.o" } },
      { NODE_LINK, .consumes = { "main.c.o" }, .produces = { "A" } },
      { NODE_PUBLISH_FILE, .consumes = { "A" }, .produces = { "build/debug/store/P/bin/A" } },
    },
  },
  {
    .name = "embed_feeds_its_header_to_compiles_and_its_object_to_the_link",
    .graph.pkgs = {
      { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .embed = { "e.txt" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_EMBED, .consumes = { "e.txt" }, .produces = { "A.embed.o", "A.embed.h" } },
      { NODE_COMPILE, .consumes = { "main.c", "A.embed.h" }, .produces = { "main.c.o" } },
      { NODE_LINK, .consumes = { "main.c.o", "A.embed.o" }, .produces = { "A" } },
      { NODE_PUBLISH_FILE, .consumes = { "A" }, .produces = { "build/debug/store/P/bin/A" } },
    },
  },
  {
    .name = "libc_sdk_feeds_compiles_and_links",
    .graph = {
      .sdk_libc = true,
      .pkgs = { { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_LIBC, .produces = { "libc.txt" } },
      { NODE_COMPILE, .consumes = { "main.c", "libc.txt" }, .produces = { "main.c.o" } },
      { NODE_LINK, .consumes = { "main.c.o", "libc.txt" }, .produces = { "A" } },
      { NODE_PUBLISH_FILE, .consumes = { "A" }, .produces = { "build/debug/store/P/bin/A" } },
    },
  },
  {
    .name = "published_include_tree_orders_consumer_compiles",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
      { .name = "B", .publish = { "b.h" } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_PUBLISH, .produces = { "build/debug/store/B/include", "build/debug/store/B/share" } },
      { NODE_COMPILE, .consumes = { "main.c" }, .orders = { "build/debug/store/B/include" }, .produces = { "main.c.o" } },
      { NODE_LINK, .consumes = { "main.c.o" }, .produces = { "A" } },
      { NODE_PUBLISH_FILE, .consumes = { "A" }, .produces = { "build/debug/store/P/bin/A" } },
    },
  },
  {
    .name = "user_node_outputs_order_the_package_compiles",
    .graph.pkgs = {
      {
        .name = "P",
        .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } },
        .nodes = { { .tag = "N", .inputs = { "in.txt" }, .outputs = { "g/x.h" } } },
      },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_USER, .consumes = { "in.txt" }, .produces = { "build/debug/.spn/P/g/x.h" } },
      { NODE_COMPILE, .consumes = { "main.c" }, .orders = { "build/debug/.spn/P/g/x.h" }, .produces = { "main.c.o" } },
      { NODE_LINK, .consumes = { "main.c.o" }, .produces = { "A" } },
      { NODE_PUBLISH_FILE, .consumes = { "A" }, .produces = { "build/debug/store/P/bin/A" } },
    },
  },
  {
    .name = "msvc_shared_dep_links_its_implib",
    .graph = {
      .os = SPN_OS_WINDOWS,
      .abi = SPN_ABI_MSVC,
      .driver = SPN_CC_DRIVER_ZIG,
      .runtime = SPN_RUNTIME_STATIC,
      .pkgs = {
        { .name = "P", .deps = { { "B" } }, .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" } } } },
        { .name = "B", .targets = { { "B", shared_only, .source = { "b.c" } } } },
      },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_COMPILE, .consumes = { "main.c" }, .produces = { "main.c.o" } },
      { NODE_COMPILE, .consumes = { "b.c" }, .produces = { "b.c.o" } },
      { NODE_EXPORTS, .consumes = { "b.c.o" }, .produces = { "B.def" } },
      { NODE_WARM, .consumes = { "stub.c" }, .produces = { "shared.c" } },
      { NODE_WARM, .consumes = { "stub.c" }, .produces = { "exe.c" } },
      { NODE_LINK, .consumes = { "b.c.o", "B.def", "shared.c" }, .produces = { "B.dll", "B.lib" } },
      { NODE_LINK, .consumes = { "main.c.o", "B.lib", "exe.c" }, .produces = { "A.exe" } },
      { NODE_PUBLISH_FILE, .consumes = { "B.dll" }, .produces = { "build/debug/store/B/lib/B.dll" } },
      { NODE_PUBLISH_FILE, .consumes = { "B.lib" }, .produces = { "build/debug/store/B/lib/B.lib" } },
      { NODE_PUBLISH_FILE, .consumes = { "A.exe" }, .produces = { "build/debug/store/P/bin/A.exe" } },
    },
  },
  {
    .name = "linker_script_is_a_link_input",
    .graph.pkgs = {
      { .name = "P", .targets = { { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "main.c" }, .linker_script = { "a.ld" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_COMPILE, .consumes = { "main.c" }, .produces = { "main.c.o" } },
      { NODE_LINK, .consumes = { "main.c.o", "a.ld" }, .produces = { "A" } },
      { NODE_PUBLISH_FILE, .consumes = { "A" }, .produces = { "build/debug/store/P/bin/A" } },
    },
  },
  {
    .name = "public_static_deps_of_a_shared_lib_feed_its_exports",
    .graph.pkgs = {
      { .name = "P", .deps = { { "B" } }, .targets = { { "A", shared_only, .source = { "a.c" } } } },
      { .name = "B", .targets = { { "B", static_only, .source = { "b.c" } } } },
    },
    .roots = { { "A", SPN_TARGET_KIND_LIB } },
    .actions = {
      { NODE_COMPILE, .consumes = { "a.c" }, .produces = { "a.c.o" } },
      { NODE_COMPILE, .consumes = { "b.c" }, .produces = { "b.c.o" } },
      { NODE_ARCHIVE, .consumes = { "b.c.o" }, .produces = { "libB.a" } },
      { NODE_EXPORTS, .consumes = { "a.c.o", "libB.a" } },
      { NODE_LINK, .consumes = { "a.c.o", "libB.a", "@exports" }, .produces = { "libA.so" } },
      { NODE_PUBLISH_FILE, .consumes = { "libB.a" }, .produces = { "build/debug/store/B/lib/libB.a" } },
      { NODE_PUBLISH_FILE, .consumes = { "libA.so" }, .produces = { "build/debug/store/P/lib/libA.so" } },
    },
  },
  {
    .name = "zig_links_share_one_warm_stub",
    .graph = {
      .driver = SPN_CC_DRIVER_ZIG,
      .pkgs = {
        { .name = "P", .targets = {
          { "A", .kind = SPN_TARGET_KIND_EXE, .source = { "a.c" } },
          { "C", .kind = SPN_TARGET_KIND_EXE, .source = { "c.c" } },
        } },
      },
    },
    .roots = { { "A", SPN_TARGET_KIND_EXE }, { "C", SPN_TARGET_KIND_EXE } },
    .actions = {
      { NODE_COMPILE, .consumes = { "a.c" }, .produces = { "a.c.o" } },
      { NODE_COMPILE, .consumes = { "c.c" }, .produces = { "c.c.o" } },
      { NODE_WARM, .consumes = { "stub.c" } },
      { NODE_LINK, .consumes = { "a.c.o", "@warm" }, .produces = { "A" } },
      { NODE_LINK, .consumes = { "c.c.o", "@warm" }, .produces = { "C" } },
      { NODE_PUBLISH_FILE, .consumes = { "A" }, .produces = { "build/debug/store/P/bin/A" } },
      { NODE_PUBLISH_FILE, .consumes = { "C" }, .produces = { "build/debug/store/P/bin/C" } },
    },
  },
};

typedef struct {
  node_t kind;
  sp_da(sp_str_t) consumes;
  sp_da(sp_str_t) orders;
  sp_da(sp_str_t) produces;
} lowered_t;

typedef struct {
  si_dag_fn_t execute;
  node_t kind;
} classified_t;

static const classified_t classified [] = {
  { si_on_compile, NODE_COMPILE },
  { si_on_compile_discover, NODE_COMPILE },
  { si_on_archive, NODE_ARCHIVE },
  { si_on_link, NODE_LINK },
  { si_on_write_exports, NODE_EXPORTS },
  { si_on_embed, NODE_EMBED },
  { si_on_publish, NODE_PUBLISH },
  { si_on_publish_file, NODE_PUBLISH_FILE },
  { si_on_user_node, NODE_USER },
  { si_on_zig_warmup, NODE_WARM },
  { si_on_write_libc, NODE_LIBC },
  { si_on_write_rsp, NODE_RSP },
};

static node_t classify(si_dag_fn_t execute) {
  sp_carr_for(classified, it) {
    if (classified[it].execute == execute) {
      return classified[it].kind;
    }
  }
  return NODE_NONE;
}

static const c8* node_name(node_t kind) {
  switch (kind) {
    case NODE_NONE: return "none";
    case NODE_COMPILE: return "compile";
    case NODE_ARCHIVE: return "archive";
    case NODE_LINK: return "link";
    case NODE_EXPORTS: return "exports";
    case NODE_EMBED: return "embed";
    case NODE_PUBLISH: return "publish";
    case NODE_PUBLISH_FILE: return "publish_file";
    case NODE_USER: return "user";
    case NODE_WARM: return "warm";
    case NODE_LIBC: return "libc";
    case NODE_RSP: return "rsp";
  }
  return "none";
}

static sp_str_t label(const spn_dag_t* g, spn_dag_id_t id) {
  const spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, id);
  return spn_path_empty(artifact->path) ? artifact->name : artifact->path.sub;
}


static void push_labels(sp_mem_t mem, const spn_dag_t* g, sp_da(sp_str_t)* labels, sp_da(spn_dag_id_t) ids) {
  sp_da_for(ids, it) {
    sp_da_push(*labels, label(g, ids[it]));
  }
}

static sp_da(lowered_t) lower_graph(sp_mem_t mem, const spn_dag_t* g) {
  sp_ht(spn_dag_id_t, u8) rsps = SP_NULLPTR;
  sp_ht_init(mem, rsps);
  sp_da_for(g->actions, it) {
    const spn_dag_action_t* action = &g->actions[it];
    if (classify(action->execute) == NODE_RSP) {
      sp_ht_insert(rsps, action->produces[0], (u8)true);
    }
  }

  sp_da(lowered_t) lowered = sp_da_new(mem, lowered_t);
  sp_da_for(g->actions, it) {
    const spn_dag_action_t* action = &g->actions[it];
    node_t kind = classify(action->execute);
    if (kind == NODE_RSP) {
      continue;
    }
    lowered_t row = { .kind = kind };
    sp_da_init(mem, row.consumes);
    sp_da_init(mem, row.orders);
    sp_da_init(mem, row.produces);
    sp_da_for(action->consumes, ct) {
      if (sp_ht_getp(rsps, action->consumes[ct])) {
        continue;
      }
      sp_da_push(row.consumes, label(g, action->consumes[ct]));
    }
    push_labels(mem, g, &row.orders, action->orders);
    push_labels(mem, g, &row.produces, action->produces);
    sp_da_push(lowered, row);
  }
  return lowered;
}

static sp_str_t render_labels(sp_mem_t mem, sp_da(sp_str_t) labels) {
  sp_str_t rendered = sp_str_lit("");
  sp_da_for(labels, it) {
    rendered = sp_fmt(mem, "{}{}{}", sp_fmt_str(rendered), sp_fmt_cstr(it ? ", " : ""), sp_fmt_str(labels[it])).value;
  }
  return rendered;
}

static sp_str_t render_lowered(sp_mem_t mem, sp_da(lowered_t) lowered) {
  sp_str_t rendered = sp_str_lit("");
  sp_da_for(lowered, it) {
    rendered = sp_fmt(mem, "{}\n  {}: [{}] +[{}] -> [{}]",
      sp_fmt_str(rendered),
      sp_fmt_cstr(node_name(lowered[it].kind)),
      sp_fmt_str(render_labels(mem, lowered[it].consumes)),
      sp_fmt_str(render_labels(mem, lowered[it].orders)),
      sp_fmt_str(render_labels(mem, lowered[it].produces))).value;
  }
  return rendered;
}

static const lowered_t* find_unique(sp_da(lowered_t) lowered, node_t kind) {
  const lowered_t* found = SP_NULLPTR;
  sp_da_for(lowered, it) {
    if (lowered[it].kind != kind) {
      continue;
    }
    if (found) {
      return SP_NULLPTR;
    }
    found = &lowered[it];
  }
  return found;
}

static sp_str_t resolve_label(sp_da(lowered_t) lowered, const c8* expect) {
  sp_str_t str = sp_cstr_as_str(expect);
  if (!sp_str_starts_with(str, sp_str_lit("@"))) {
    return str;
  }
  sp_for(kind, NODE_RSP + 1) {
    if (sp_str_equal_cstr(sp_str_suffix(str, str.len - 1), node_name((node_t)kind))) {
      const lowered_t* action = find_unique(lowered, (node_t)kind);
      return action && !sp_da_empty(action->produces) ? action->produces[0] : sp_str_lit("");
    }
  }
  return sp_str_lit("");
}

static const lowered_t* find_action(sp_da(lowered_t) lowered, const action_t* expect) {
  if (!expect->produces[0]) {
    return find_unique(lowered, expect->kind);
  }
  sp_da_for(lowered, it) {
    if (lowered[it].kind == expect->kind && !sp_da_empty(lowered[it].produces) && sp_str_ends_with(lowered[it].produces[0], sp_cstr_as_str(expect->produces[0]))) {
      return &lowered[it];
    }
  }
  return SP_NULLPTR;
}

static sp_err_t expect_labels(sp_test_t* t, sp_da(lowered_t) lowered, const c8* field, sp_da(sp_str_t) actual, const c8* const* expect, u32 count) {
  sp_test_kv_c(t, "field", field);
  sp_must_eq(t, count, (u32)sp_da_size(actual));
  bool* used = sp_alloc_n(sp_test_arena(t), bool, count ? count : 1);
  sp_mem_zero(used, sizeof(bool) * count);
  sp_for(et, count) {
    sp_str_t wanted = resolve_label(lowered, expect[et]);
    sp_test_kv(t, "label", wanted);
    sp_must(t, !sp_str_empty(wanted));
    u32 matches = 0;
    sp_da_for(actual, at) {
      if (!used[at] && sp_str_ends_with(actual[at], wanted)) {
        used[at] = true;
        matches++;
        break;
      }
    }
    sp_expect_eq(t, 1, matches);
  }
  return SP_OK;
}

sp_test_each(unit_lower, graph, test_t, tests, .setup = spn_test_ctx_setup) {
  sp_mem_t mem = sp_test_arena(t);
  units_t* u = units_new(t, &it->graph);
  u32 roots = 0;
  sp_carr_detect_len(it->roots, roots, it->roots[roots].name);
  si_da(spn_target_unit_t*) targets = sp_zero;
  sp_must_eq(t, SPN_OK, units_add_roots(u, it->roots, roots, &targets));
  sp_must_eq(t, SPN_OK, units_plan(u, targets));

  spn_op_t op = { .ctx = &u->ctx, .session = &u->session };
  spn_dag_build_t* b = spn_dag_build_new(&op);
  sp_must_eq(t, SPN_OK, spn_dag_build_lower(b, &u->plan));
  sp_must_eq(t, SPN_OK, spn_dag_validate(b->graph).err);

  sp_da(lowered_t) lowered = lower_graph(mem, b->graph);
  sp_str_t graph = render_lowered(mem, lowered);
  sp_test_kv(t, "graph", graph);
  u32 expected = 0;
  sp_carr_detect_len(it->actions, expected, it->actions[expected].kind);
  sp_must_eq(t, expected, (u32)sp_da_size(lowered));

  sp_for(at, expected) {
    const action_t* expect = &it->actions[at];
    sp_test_kv_clear(t, SP_NULLPTR);
    sp_test_kv(t, "graph", graph);
    sp_test_kv_c(t, "action", node_name(expect->kind));
    const lowered_t* action = find_action(lowered, expect);
    sp_must(t, action);
    u32 consumes = 0;
    u32 orders = 0;
    u32 produces = 0;
    sp_carr_detect_len(expect->consumes, consumes, expect->consumes[consumes]);
    sp_carr_detect_len(expect->orders, orders, expect->orders[orders]);
    sp_carr_detect_len(expect->produces, produces, expect->produces[produces]);
    sp_try(expect_labels(t, lowered, "consumes", action->consumes, expect->consumes, consumes));
    sp_try(expect_labels(t, lowered, "orders", action->orders, expect->orders, orders));
    if (produces) {
      sp_try(expect_labels(t, lowered, "produces", action->produces, expect->produces, produces));
    }
  }
  return SP_OK;
}
