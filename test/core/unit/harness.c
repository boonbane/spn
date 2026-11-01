#include "unit.h"

#include "event/event.h"
#include "hash/digest/digest.h"
#include "paths/paths.h"
#include "pkg/id.h"
#include "profile/profile.h"
#include "toolchain/toolchain.h"
#include "toolchain/sdk.h"
#include "when/when.h"

static sp_da(sp_str_t) strs(sp_mem_t mem, const c8* const* items, u32 max) {
  sp_da(sp_str_t) list = sp_da_new(mem, sp_str_t);
  sp_for(it, max) {
    if (!items[it]) {
      break;
    }
    sp_da_push(list, sp_cstr_as_str(items[it]));
  }
  return list;
}

static spn_gated_list_t gated_list(sp_mem_t mem, const c8* const* items, u32 max) {
  spn_gated_list_t list = SP_NULLPTR;
  sp_for(it, max) {
    if (!items[it]) {
      break;
    }
    si_da_push(mem, list, ((spn_gated_str_t) { .value = sp_cstr_as_str(items[it]) }));
  }
  return list;
}

static spn_gated_path_list_t gated_paths(sp_mem_t mem, const c8* const* items, u32 max) {
  spn_gated_path_list_t list = SP_NULLPTR;
  sp_for(it, max) {
    if (!items[it]) {
      break;
    }
    si_da_push(mem, list, ((spn_gated_path_t) { .path = sp_cstr_as_str(items[it]), .tree = SPN_TREE_SOURCE }));
  }
  return list;
}

static si_da(spn_gated_source_t) gated_sources(sp_mem_t mem, const c8* const* items, u32 max) {
  si_da(spn_gated_source_t) list = SP_NULLPTR;
  sp_for(it, max) {
    if (!items[it]) {
      break;
    }
    sp_str_t path = sp_cstr_as_str(items[it]);
    si_da_push(mem, list, ((spn_gated_source_t) {
      .kind = sp_str_contains(path, sp_str_lit("*")) || sp_str_contains(path, sp_str_lit("[")) ? SPN_SOURCE_GLOB : SPN_SOURCE_FILE,
      .path = path,
      .tree = SPN_TREE_SOURCE,
    }));
  }
  return list;
}

static si_da(spn_gated_embed_t) gated_embeds(sp_mem_t mem, const c8* const* items, u32 max) {
  si_da(spn_gated_embed_t) list = SP_NULLPTR;
  sp_for(it, max) {
    if (!items[it]) {
      break;
    }
    si_da_push(mem, list, ((spn_gated_embed_t) {
      .kind = SPN_EMBED_FILE,
      .path = sp_cstr_as_str(items[it]),
      .tree = SPN_TREE_SOURCE,
      .dest = sp_cstr_as_str(items[it]),
    }));
  }
  return list;
}

static si_da(spn_gated_publish_t) gated_publish(sp_mem_t mem, const c8* const* items, u32 max) {
  si_da(spn_gated_publish_t) list = SP_NULLPTR;
  sp_for(it, max) {
    if (!items[it]) {
      break;
    }
    si_da_push(mem, list, ((spn_gated_publish_t) {
      .root = SPN_PUBLISH_ROOT_INCLUDE,
      .source = { .kind = SPN_SOURCE_FILE, .path = sp_cstr_as_str(items[it]), .tree = SPN_TREE_SOURCE },
    }));
  }
  return list;
}

static void declare_target(sp_mem_t mem, si_pkg_info_t* manifest, const graph_target_t* spec) {
  si_target_info_t* decl = test_manifest_target(mem, manifest, sp_cstr_as_str(spec->name), spec->kind);
  decl->linkages = spec->linkages;
  decl->no_link = spec->no_link;
  decl->source = gated_sources(mem, spec->source, UNIT_TEST_MAX_STRS);
  decl->deps = gated_list(mem, spec->deps, UNIT_TEST_MAX_STRS);
  decl->include = gated_paths(mem, spec->include, UNIT_TEST_MAX_STRS);
  decl->define = gated_list(mem, spec->define, UNIT_TEST_MAX_STRS);
  decl->system_deps = gated_list(mem, spec->system_deps, UNIT_TEST_MAX_STRS);
  decl->macos.frameworks = gated_list(mem, spec->frameworks, UNIT_TEST_MAX_STRS);
  decl->macos.min_os = spec->min_os;
  decl->link_flags = gated_list(mem, spec->link_flags, UNIT_TEST_MAX_STRS);
  decl->linker_script = gated_paths(mem, spec->linker_script, UNIT_TEST_MAX_STRS);
  decl->embed = gated_embeds(mem, spec->embed, UNIT_TEST_MAX_STRS);
}

static void declare_options(sp_mem_t mem, si_pkg_info_t* manifest, spn_resolved_pkg_t* resolved, const graph_pkg_t* spec) {
  sp_da_init(mem, resolved->options.values);
  sp_carr_for(spec->options, it) {
    const graph_option_t* option = &spec->options[it];
    if (!option->name) {
      break;
    }
    sp_intern_str_t name = spn_intern(sp_cstr_as_str(option->name));
    si_om_insert(mem, manifest->options, name.id, ((spn_option_info_t) {
      .id = name.id,
      .name = name,
      .public = option->public,
      .define = option->define ? sp_cstr_as_str(option->define) : sp_str_lit(""),
    }));
    sp_da_push(resolved->options.values, ((spn_resolved_option_t) {
      .name = name,
      .value = spn_option_value_bool(option->value),
      .is_default = option->is_default,
    }));
  }
  resolved->options.decls = manifest->options;
}

static spn_toolchain_info_t* toolchain_info(sp_mem_t mem, const graph_t* g) {
  spn_toolchain_info_t* info = sp_alloc_type(mem, spn_toolchain_info_t);
  sp_intern_str_t name = spn_intern_lit("T");
  *info = (spn_toolchain_info_t) {
    .id = name.id,
    .name = name,
    .driver = g->driver ? g->driver : SPN_CC_DRIVER_GCC,
    .compiler.program = spn_arg_lit(sp_cstr_as_str(g->cc ? g->cc : "cc")),
    .archiver.program = spn_arg_lit(sp_str_lit("ar")),
    .link_args = strs(mem, g->link_args, UNIT_TEST_MAX_STRS),
    .support = { .kind = SPN_TOOLCHAIN_SUPPORT_LOCAL },
  };
  if (!g->no_cxx) {
    info->cxx.program = spn_arg_lit(sp_str_lit("c++"));
  }
  if (g->sha256) {
    info->support = (spn_toolchain_support_t) { .kind = SPN_TOOLCHAIN_SUPPORT_ARTIFACT, .artifact.sha256 = sp_cstr_as_str(g->sha256) };
  }
  return info;
}

static spn_profile_info_t program_profile(units_t* u, const graph_t* g, const spn_toolchain_info_t* info) {
  sp_intern_str_t name = spn_intern_lit("debug");
  spn_profile_info_t profile = {
    .id = name.id,
    .name = name,
    .toolchain = { .kind = SPN_TOOLCHAIN_REF_AUTO },
    .driver = info->driver,
    .standard = g->standard ? g->standard : SPN_C11,
    .mode = g->mode ? g->mode : SPN_MODE_DEBUG,
    .opt = g->opt,
    .sanitizers = g->sanitize ? 1u << g->sanitize : 0,
    .os = g->os ? g->os : SPN_OS_LINUX,
    .arch = SPN_ARCH_X64,
    .abi = g->abi ? g->abi : (g->os == SPN_OS_MACOS ? SPN_ABI_NONE : SPN_ABI_GNU),
    .request = g->request,
    .linking.linkage = g->linkage,
    .linking.runtime = g->runtime,
    .linking.libc = g->libc,
  };
  spn_triple_t triple = spn_profile_triple(&profile);
  profile.linker = spn_ld_native(profile.driver, triple);
  if (g->sdk) {
    profile.sdk = spn_sdk_at(u->mem, triple, (spn_path_t) { .sub = sp_cstr_as_str(g->sdk) });
  }
  if (g->sdk_libc) {
    profile.sdk = (spn_sdk_t) {
      .kind = SPN_SDK_LIBC,
      .libc = {
        .include = spn_path_make(&u->ctx.roots, sp_str_lit("/libc/include")),
        .sys_include = spn_path_make(&u->ctx.roots, sp_str_lit("/libc/sys")),
        .crt = spn_path_make(&u->ctx.roots, sp_str_lit("/libc/crt")),
      },
    };
  }
  return profile;
}

static void bind_cc(spn_toolchain_unit_t* unit) {
  spn_toolchain_info_t* info = unit->info;
  unit->cc = (spn_cc_t) {
    .name = info->name,
    .driver = info->driver,
    .compiler = info->compiler,
    .cxx = info->cxx,
    .archiver = info->archiver,
    .link_args = info->link_args,
    .archiver_driver = SPN_AR_DRIVER_GNU,
  };
  if (info->support.kind == SPN_TOOLCHAIN_SUPPORT_ARTIFACT) {
    unit->identity = spn_digest_hash_str(info->support.artifact.sha256);
  }
}

static spn_pkg_id_t pkg_id(const c8* name) {
  return (spn_pkg_id_t) { .qualified = spn_intern(spn_pkg_canonicalize_name(sp_cstr_as_str(name))).id };
}

static const graph_pkg_t* pkg_spec(units_t* u, const c8* name) {
  sp_carr_for(u->graph->pkgs, it) {
    const graph_pkg_t* spec = &u->graph->pkgs[it];
    if (!spec->name) {
      break;
    }
    if (sp_cstr_equal(spec->name, name)) {
      return spec;
    }
  }
  return SP_NULLPTR;
}

static void open_ctx(units_t* u, sp_test_t* t) {
  sp_mem_t mem = u->mem;
  u->ctx = (spn_ctx_t) {
    .mem = spn.mem,
    .heap = spn.heap,
    .intern = spn.intern,
    .env = spn.env,
    .host = spn.host,
    .events = spn_event_buffer_new(mem),
  };
  sp_path_t storage = sp_path_join(mem, sp_test_dir(t), sp_str_lit("storage"));
  sp_fs_create_dir_at(storage);
  sp_assert(!spn_path_roots_set(&u->ctx.roots, mem, SPN_PATH_ROOT_PROJECT, sp_test_dir(t)));
  sp_assert(!spn_path_roots_set(&u->ctx.roots, mem, SPN_PATH_ROOT_STORAGE, storage));
}

units_t* units_new(sp_test_t* t, const graph_t* g) {
  sp_mem_t mem = sp_test_arena(t);
  units_t* u = sp_alloc_type(mem, units_t);
  u->mem = mem;
  u->graph = g;
  open_ctx(u, t);
  u->session = (spn_session_t) { .ctx = &u->ctx, .mem = mem };
  sp_ht_init(mem, u->resolve);

  spn_toolchain_info_t* info = toolchain_info(mem, g);
  spn_profile_info_t profile = program_profile(u, g, info);
  spn_profile_info_t metaprogram = spn_profile_metaprogram();
  metaprogram.driver = profile.driver;
  metaprogram.linker = spn_ld_native(metaprogram.driver, spn_profile_triple(&metaprogram));

  spn_path_t project = spn_path_from_root(SPN_PATH_ROOT_PROJECT);
  spn_build_unit_t* program = spn_build_add(&u->session, profile, spn_path_join(mem, project, sp_str_lit("build/debug")), info);
  spn_build_unit_t* metabuild = spn_build_add(&u->session, metaprogram, spn_path_join(mem, project, sp_str_lit("build/wasm32-wasi")), info);
  program->metabuild = metabuild;
  u->session.builds.program = program;
  u->session.builds.metaprogram = metabuild;
  bind_cc(program->toolchain);

  u32 count = 0;
  sp_carr_detect_len(g->pkgs, count, g->pkgs[count].name);
  sp_for(it, count) {
    const graph_pkg_t* spec = &g->pkgs[it];

    si_pkg_info_t* manifest = sp_alloc_type(mem, si_pkg_info_t);
    *manifest = test_manifest_make(sp_cstr_as_str(spec->name));
    manifest->qualified = spn_intern(spn_pkg_canonicalize_name(manifest->name.str));
    manifest->include = gated_paths(mem, spec->include, UNIT_TEST_MAX_STRS);
    manifest->define = gated_list(mem, spec->define, UNIT_TEST_MAX_STRS);
    manifest->system_deps = gated_list(mem, spec->system_deps, UNIT_TEST_MAX_STRS);
    manifest->macos.frameworks = gated_list(mem, spec->frameworks, UNIT_TEST_MAX_STRS);
    manifest->macos.min_os = spec->min_os;
    manifest->publish = gated_publish(mem, spec->publish, UNIT_TEST_MAX_STRS);
    sp_carr_for(spec->targets, tt) {
      if (!spec->targets[tt].name) {
        break;
      }
      declare_target(mem, manifest, &spec->targets[tt]);
    }

    spn_path_t tree = spn_path_join(mem, project, sp_fmt(mem, "pkg/{}", sp_fmt_cstr(spec->name)).value);
    spn_pkg_source_t source = spec->source;
    if (it && !source) {
      source = SPN_PKG_SOURCE_FILE;
    }
    spn_resolved_pkg_t resolved = {
      .id = pkg_id(spec->name),
      .name = manifest->name,
      .source = source,
      .manifest = manifest,
      .roots = { .recipe = tree, .source = tree },
    };
    if (spec->config) {
      spn_pkg_config_t* config = sp_alloc_type(mem, spn_pkg_config_t);
      *config = (spn_pkg_config_t) { .name = manifest->name, .kind = { .some = true, .value = spec->config } };
      resolved.config = config;
    }
    declare_options(mem, manifest, &resolved, spec);
    sp_da_init(mem, resolved.edges);
    sp_carr_for(spec->deps, dt) {
      if (!spec->deps[dt].to) {
        break;
      }
      sp_da_push(resolved.edges, ((spn_resolved_dep_t) {
        .id = pkg_id(spec->deps[dt].to),
        .kind = spec->deps[dt].kind,
        .private = spec->deps[dt].private,
      }));
    }
    sp_ht_insert(u->resolve, resolved.id, resolved);
  }

  u->configure = (spn_units_t) { .ctx = &u->ctx, .mem = mem, .resolve = u->resolve };
  u->units = (spn_units_t) { .ctx = &u->ctx, .mem = mem, .resolve = u->resolve, .configure = &u->configure };
  return u;
}

spn_resolved_pkg_t* units_resolved(units_t* u, const c8* pkg) {
  return sp_ht_getp(u->resolve, pkg_id(pkg));
}

si_pkg_info_t* units_manifest(units_t* u, const c8* pkg) {
  return (si_pkg_info_t*)units_resolved(u, pkg)->manifest;
}

static void add_nodes(units_t* u, spn_pkg_unit_t* unit, const graph_pkg_t* spec) {
  sp_mem_t mem = u->mem;
  sp_carr_for(spec->nodes, nt) {
    const graph_node_t* node = &spec->nodes[nt];
    if (!node->tag) {
      break;
    }
    spn_user_node_t made = { .unit = unit, .tag = sp_cstr_as_str(node->tag) };
    sp_carr_for(node->inputs, it) {
      if (!node->inputs[it]) {
        break;
      }
      si_da_push(mem, made.inputs, spn_path_join(mem, unit->paths.roots.source, sp_cstr_as_str(node->inputs[it])));
    }
    sp_carr_for(node->outputs, ot) {
      if (!node->outputs[ot]) {
        break;
      }
      sp_str_t sub = sp_cstr_as_str(node->outputs[ot]);
      si_da_push(mem, made.outputs, ((spn_user_output_t) {
        .dir = SPN_DIR_WORK,
        .sub = sub,
        .kind = SPN_DAG_ARTIFACT_KIND_FILE,
        .path = spn_path_join(mem, unit->paths.work, sub),
      }));
    }
    sp_da_push(unit->user_nodes, made);
  }
}

spn_err_t units_add_package(units_t* u, const c8* pkg, spn_pkg_unit_t** unit) {
  bool known = sp_om_has(u->units.packages, ((spn_pkg_unit_id_t) { .pkg = pkg_id(pkg), .build = u->session.builds.program->id }));
  spn_try(si_add_package_unit(&u->units, u->session.builds.program, units_resolved(u, pkg), unit));
  if (!known) {
    add_nodes(u, *unit, pkg_spec(u, pkg));
  }
  return SPN_OK;
}

spn_err_t units_add_target(units_t* u, spn_pkg_unit_t* pkg, graph_root_t root, spn_target_unit_t** unit) {
  si_target_t* target = spn_pkg_get_target(&pkg->pkg, sp_cstr_as_str(root.name), root.kind);
  sp_assert(target);
  return si_add_target_unit(&u->units, pkg, target, unit);
}

spn_err_t units_add_roots(units_t* u, const graph_root_t* roots, u32 count, si_da(spn_target_unit_t*)* targets) {
  const c8* name = u->graph->pkgs[0].name;
  si_pkg_info_t* manifest = units_manifest(u, name);
  sp_for(it, count) {
    si_target_id_t id = { .name = spn_intern(sp_cstr_as_str(roots[it].name)).id, .kind = roots[it].kind };
    if (!si_om_has(manifest->targets, id)) {
      test_manifest_target(u->mem, manifest, sp_cstr_as_str(roots[it].name), roots[it].kind);
    }
  }

  spn_pkg_unit_t* root = SP_NULLPTR;
  spn_try(units_add_package(u, name, &root));
  sp_for(it, count) {
    spn_target_unit_t* unit = SP_NULLPTR;
    spn_try(units_add_target(u, root, roots[it], &unit));
    si_da_push(u->mem, *targets, unit);
  }
  return SPN_OK;
}

spn_err_t units_plan(units_t* u, si_da(spn_target_unit_t*) targets) {
  spn_try(spn_plan_new(&u->configure, SP_NULLPTR, &u->configured));
  return spn_plan_new(&u->units, targets, &u->plan);
}

spn_target_unit_t* units_find_target(units_t* u, const c8* name, spn_target_kind_t kind) {
  sp_om_for(u->units.targets, it) {
    spn_target_unit_t* unit = sp_om_at(u->units.targets, it);
    if (unit->target->info->kind == kind && sp_str_equal_cstr(unit->target->info->name.str, name)) {
      return unit;
    }
  }
  return SP_NULLPTR;
}

sp_da(spn_event_t) units_errs(units_t* u) {
  sp_da(spn_event_t) errs = sp_da_new(u->mem, spn_event_t);
  sp_da(spn_event_t) events = spn_event_buffer_drain(u->mem, u->ctx.events);
  sp_da_for(events, it) {
    if (events[it].kind == SPN_EVENT_ERR) {
      sp_da_push(errs, events[it]);
    }
  }
  return errs;
}
