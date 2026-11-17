#include "session/session.h"

#include "sp.h"
#include "ctx/types.h"
#include "spn/errors.h"
#include "core/types.h"
#include "resolve/types.h"
#include "session/types.h"
#include "spn/core.h"
#include "spn/types.h"
#include "unit/types.h"
#include "unit/unit.h"

#include "compiler/driver.h"
#include "error/error.h"
#include "event/event.h"
#include "hash/digest/digest.h"
#include "intern/intern.h"
#include "paths/paths.h"
#include "project/types.h"
#include "pkg/pkg.h"
#include "profile/profile.h"
#include "toolchain/select.h"
#include "toolchain/toolchain.h"
#include "triple/triple.h"
#include "when/when.h"

static spn_toolchain_unit_t* bind_toolchain(spn_session_t* s, spn_toolchain_info_t* toolchain) {
  si_da_for_value(s->builds.toolchains, unit) {
    if (unit->info == toolchain) {
      return unit;
    }
  }

  spn_toolchain_unit_t* unit = sp_alloc_type(s->mem, spn_toolchain_unit_t);
  *unit = (spn_toolchain_unit_t) { .info = toolchain };
  si_da_push(s->mem, s->builds.toolchains, unit);
  return unit;
}

static spn_build_unit_t* add_build(spn_session_t* s, spn_profile_info_t profile, spn_path_t root, spn_toolchain_info_t* toolchain) {
  sp_hash_t parts [] = {
    spn_digest_hash_str(profile.name.str),
    spn_digest_hash_str(toolchain->name.str),
    spn_sdk_hash(&profile.sdk),
    (sp_hash_t)profile.target.os,
    (sp_hash_t)profile.target.arch,
    (sp_hash_t)profile.target.abi,
    (sp_hash_t)profile.driver,
    (sp_hash_t)profile.linker,
    (sp_hash_t)profile.linking.linkage,
    (sp_hash_t)profile.linking.runtime,
    (sp_hash_t)profile.linking.libc,
    (sp_hash_t)profile.standard,
    (sp_hash_t)profile.mode,
    (sp_hash_t)profile.opt,
    (sp_hash_t)profile.targeted,
    spn_digest_hash(&profile.sanitizers, sizeof(profile.sanitizers)),
  };
  spn_build_unit_t* build = sp_alloc_type(s->mem, spn_build_unit_t);
  *build = (spn_build_unit_t) {
    .id = spn_digest_hash_combine(parts, sp_carr_len(parts)),
    .profile = profile,
    .toolchain = bind_toolchain(s, toolchain),
    .paths = { .root = root },
    .define = spn_when_facts_to_defines(s->mem, spn_profile_facts(&profile)),
  };
  sp_da_init(s->mem, build->include);
  return build;
}

spn_err_t spn_session_init(spn_session_t* s, spn_ctx_t* ctx, sp_mem_t mem, spn_project_t* project, spn_session_config_t config) {
  s->ctx = ctx;
  s->project = project;
  s->arena = sp_mem_arena_new(mem);
  s->mem = sp_mem_arena_as_allocator(s->arena);
  s->manifest = &project->package;
  s->force = config.force;
  s->paths.root = spn_path_from_root(SPN_PATH_ROOT_PROJECT);
  s->paths.build = spn_path_project_build();

  sp_ht_init(s->mem, s->registry);

  spn_profile_info_t profile = sp_zero;
  spn_try(spn_profile_resolve(&config.profile, ctx->host, project, &profile));

  spn_toolchain_query_t query = sp_zero;
  spn_try(spn_profile_query(&profile, ctx->host, &query));
  spn_toolchain_selection_t target = sp_zero;
  spn_try(spn_toolchain_select(&ctx->catalog, query, &target));
  spn_profile_finalize(&profile, &target);

  spn_profile_info_t metaprogram = spn_profile_metaprogram();
  spn_toolchain_query_t metaprogram_query = sp_zero;
  spn_try(spn_profile_query(&metaprogram, ctx->host, &metaprogram_query));
  spn_toolchain_selection_t script = sp_zero;
  spn_try(spn_toolchain_select(&ctx->catalog, metaprogram_query, &script));
  spn_profile_finalize(&metaprogram, &script);

  struct { spn_path_t build; spn_path_t metabuild; } roots = {
    .build = spn_path_join(s->mem, s->paths.build, spn_profile_build_dir(s->mem, &profile)),
    .metabuild = spn_path_join(s->mem, s->paths.build, spn_triple_to_str(s->mem, metaprogram.target))
  };
  s->builds.program = add_build(s, profile, roots.build, target.toolchain);
  s->builds.metaprogram = add_build(s, metaprogram, roots.metabuild, script.toolchain);
  s->builds.program->metabuild = s->builds.metaprogram;

  sp_da_push(s->builds.metaprogram->include, spn_path(s->mem, SPN_DIR_ID_RUNTIME, "include"));

  s->selection.kinds = config.selection.kinds;
  sp_for(it, config.selection.names.count) {
    sp_intern_str_t name = spn_intern(config.selection.names.items[it]);
    si_om_insert(s->mem, s->selection.names, name.id, name);
  }

  return SPN_OK;
}
