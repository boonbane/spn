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
#include "intern/intern.h"
#include "paths/paths.h"
#include "project/types.h"
#include "pkg/pkg.h"
#include "profile/profile.h"
#include "toolchain/select.h"
#include "triple/triple.h"

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

  spn_triple_t metaprogram_triple = { metaprogram.arch, metaprogram.os, metaprogram.abi };
  struct { spn_path_t build; spn_path_t metabuild; } roots = {
    .build = spn_path_join(s->mem, s->paths.build, spn_profile_build_dir(s->mem, &profile)),
    .metabuild = spn_path_join(s->mem, s->paths.build, spn_triple_to_str(s->mem, metaprogram_triple))
  };
  s->builds.program = spn_build_add(s, profile, roots.build, target.toolchain);
  s->builds.metaprogram = spn_build_add(s, metaprogram, roots.metabuild, script.toolchain);

  sp_da_push(s->builds.metaprogram->include, spn_path(s->mem, SPN_DIR_ID_RUNTIME, "include"));

  spn_path_t log_path = spn_path_join(s->mem, s->builds.program->paths.root, sp_str_lit(".spn/build.jsonl"));
  if (spn_event_log_open(ctx->events, spn_path_at(&ctx->roots, log_path))) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_FS_WRITE,
      .fs = { .path = log_path },
    });
  }

  s->selection.kinds = config.selection.kinds;
  sp_for(it, config.selection.names.count) {
    sp_intern_str_t name = spn_intern(config.selection.names.items[it]);
    si_om_insert(s->mem, s->selection.names, name.id, name);
  }

  return SPN_OK;
}

spn_resolved_pkg_t* si_get_root_pkg(spn_session_t* session) {
  sp_ht_for_kv(session->resolve, it) {
    if (it.val->source == SPN_PKG_SOURCE_ROOT) {
      return it.val;
    }
  }
  return SP_NULLPTR;
}
