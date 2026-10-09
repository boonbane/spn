#include "session/session.h"

#include "sp.h"
#include "ctx/types.h"
#include "spn/errors.h"
#include "core/types.h"
#include "resolve/types.h"
#include "session/types.h"
#include "spn/core.h"
#include "unit/types.h"
#include "unit/unit.h"

#include "compiler/driver.h"
#include "error/error.h"
#include "event/event.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "project/types.h"
#include "pkg/pkg.h"
#include "pkg/options.h"
#include "profile/profile.h"
#include "toolchain/select.h"
#include "triple/triple.h"

static spn_session_config_t copy_config(sp_mem_t mem, spn_session_config_t config) {
  spn_str_arr_t names = {
    .items = sp_alloc_n(mem, sp_str_t, config.selection.names.count),
    .count = config.selection.names.count,
  };
  sp_for(it, names.count) {
    names.items[it] = sp_str_copy(mem, config.selection.names.items[it]);
  }
  return (spn_session_config_t) {
    .selection = {
      .kinds = config.selection.kinds,
      .names = names,
    },
    .profile = {
      .name = sp_str_copy(mem, config.profile.name),
      .toolchain = sp_str_copy(mem, config.profile.toolchain),
      .mode = config.profile.mode,
      .opt = config.profile.opt,
      .sanitizers = config.profile.sanitizers,
      .sanitizers_set = config.profile.sanitizers_set,
      .triple = config.profile.triple,
    },
    .force = config.force,
  };
}

spn_err_t spn_session_init(spn_session_t* s, spn_ctx_t* ctx, sp_mem_t mem, spn_project_t* project, spn_session_config_t config) {
  spn_pkg_info_t* root = &project->package;
  s->ctx = ctx;
  s->project = project;
  s->arena = sp_mem_arena_new(mem);
  s->mem = sp_mem_arena_as_allocator(s->arena);
  s->pkg = root;
  config = copy_config(s->mem, config);
  s->config = config;
  s->paths.root = spn_path_from_root(SPN_PATH_ROOT_PROJECT);
  s->paths.build = spn_path_join(s->mem, s->paths.root, sp_str_lit("build"));
  spn_triple_t host = ctx->host;

  sp_ht_init(s->mem, s->registry);
  sp_ht_init(s->mem, s->packages);
  sp_ht_init(s->mem, s->options);
  sp_ht_init(s->mem, s->fingerprints);
  sp_da_init(s->mem, s->plans.build);
  sp_da_init(s->mem, s->units.toolchains);
  sp_om_new(s->units.builds);
  sp_om_new(s->units.packages);
  sp_om_new(s->units.targets);
  sp_om_new(s->units.objects);
  sp_om_new(s->plans.targets);
  sp_om_new(s->plans.objects);
  sp_om_new(s->dag.objects);

  spn_try(spn_profile_resolve(&config.profile, host, root, &s->profile));

  spn_toolchain_query_t query = sp_zero;
  spn_try(spn_profile_query(&s->profile, host, &query));
  spn_toolchain_selection_t target = sp_zero;
  spn_try(spn_toolchain_select(&ctx->catalog, query, &target));
  spn_profile_finalize(&s->profile, &target);

  spn_profile_info_t metaprogram = spn_profile_metaprogram();
  spn_toolchain_query_t metaprogram_query = sp_zero;
  spn_try(spn_profile_query(&metaprogram, host, &metaprogram_query));
  spn_toolchain_selection_t script = sp_zero;
  spn_try(spn_toolchain_select(&ctx->catalog, metaprogram_query, &script));
  spn_profile_finalize(&metaprogram, &script);

  spn_path_t target_root = spn_path_join(s->mem, s->paths.build, spn_profile_build_dir(s->mem, &s->profile));
  s->units.target = spn_build_add(s, s->profile, target_root, target.toolchain);

  spn_triple_t metaprogram_triple = { metaprogram.arch, metaprogram.os, metaprogram.abi };
  spn_path_t metaprogram_root = spn_path_join(s->mem, s->paths.build, spn_triple_to_str(s->mem, metaprogram_triple));
  s->units.metaprogram = spn_build_add(s, metaprogram, metaprogram_root, script.toolchain);
  sp_da_push(s->units.metaprogram->include, spn_path(s->mem, SPN_DIR_ID_RUNTIME, "include"));

  spn_path_t log_path = spn_path_join(s->mem, s->units.target->paths.root, sp_str_lit(".spn/build.jsonl"));
  if (spn_event_log_open(ctx->events, spn_path_at(&ctx->roots, log_path))) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_FS_WRITE,
      .fs = { .path = log_path },
    });
  }

  spn_build_plan_t plan = {
    .build = s->units.target,
    .selection = config.selection,
  };
  sp_da_init(s->mem, plan.roots);
  sp_da_init(s->mem, plan.staged);
  sp_da_push(s->plans.build, plan);

  return SPN_OK;
}

sp_opt_spn_linkage_t spn_session_config_kind(spn_session_t* session, sp_str_t pkg_name) {
  sp_opt_spn_linkage_t requested = sp_zero;

  spn_pkg_config_t* config = spn_pkg_config_find(session->pkg->config, pkg_name);
  if (config && !sp_opt_is_null(config->kind)) {
    sp_opt_set(requested, config->kind.value);
  }

  return requested;
}

spn_pkg_id_t spn_session_root_pkg(spn_session_t* session) {
  sp_ht_for_kv(session->resolve, it) {
    if (it.val->source == SPN_PKG_SOURCE_ROOT) {
      return it.val->id;
    }
  }
  return SP_ZERO_STRUCT(spn_pkg_id_t);
}

spn_pkg_unit_t* spn_session_find_pkg_unit_by_id(spn_session_t* session, spn_pkg_unit_id_t id) {
  return sp_om_has(session->units.packages, id) ? sp_om_get(session->units.packages, id) : SP_NULLPTR;
}

spn_pkg_unit_t* spn_session_find_pkg_unit(spn_session_t* session, spn_build_unit_t* build, spn_pkg_id_t pkg) {
  return spn_session_find_pkg_unit_by_id(session, (spn_pkg_unit_id_t) {
    .pkg = pkg,
    .build = build->id,
  });
}

spn_pkg_unit_t* spn_session_find_dep(spn_session_t* session, spn_pkg_unit_t* pkg, sp_str_t qualified, spn_dep_kind_t kind) {
  sp_intern_id_t name = sp_intern(session->ctx->intern, qualified).id;

  sp_da_for(pkg->deps, it) {
    if (pkg->deps[it].kind != kind) {
      continue;
    }
    if (pkg->deps[it].unit && pkg->deps[it].unit->id.pkg.qualified == name) {
      return pkg->deps[it].unit;
    }
  }
  return SP_NULLPTR;
}

spn_target_unit_t* spn_session_find_target_in_pkg(spn_session_t* session, spn_pkg_unit_t* pkg, spn_target_key_t key) {
  spn_target_unit_id_t id = { .pkg = pkg->id, .target = key };
  return sp_om_has(session->units.targets, id) ? sp_om_get(session->units.targets, id) : SP_NULLPTR;
}

spn_target_unit_t* spn_session_get_target_unit(spn_session_t* session, spn_target_unit_id_t id) {
  sp_assert(sp_om_has(session->units.targets, id));
  return sp_om_get(session->units.targets, id);
}

spn_target_plan_t* spn_session_get_target_plan(spn_session_t* session, spn_target_unit_id_t id) {
  sp_assert(sp_om_has(session->plans.targets, id));
  return sp_om_get(session->plans.targets, id);
}

spn_invocation_t* spn_session_get_object_plan(spn_session_t* session, spn_compile_unit_id_t id) {
  sp_assert(sp_om_has(session->plans.objects, id));
  return sp_om_get(session->plans.objects, id);
}

