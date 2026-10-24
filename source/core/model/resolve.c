#include "sp.h"
#include "macro/macro.h"
#include "ctx/types.h"
#include "intern/types.h"
#include "project/types.h"
#include "resolve/types.h"

#include "error/error.h"
#include "event/event.h"
#include "index/cache.h"
#include "op/types.h"
#include "intern/intern.h"
#include "pkg/id.h"
#include "pkg/load.h"
#include "paths/paths.h"
#include "resolve/resolve.h"
#include "semver/convert.h"
#include "session/session.h"
#include "unit/types.h"

static spn_err_t apply_patch_overrides(spn_session_t* session, spn_resolve_query_t* query) {
  spn_err_t result = SPN_OK;
  sp_ht_for_kv(query->result, it) {
    spn_resolved_pkg_t* pkg = it.val;
    if (pkg->source != SPN_PKG_SOURCE_INDEX) {
      continue;
    }

    spn_path_t patch = spn_path_join(spn.mem, session->ctx->paths.patches, pkg->origin.release->id.name);
    spn_path_t path = spn_path_join(spn.mem, patch, pkg->origin.paths.manifest);
    if (!sp_fs_exists_at(spn_path_at(&session->ctx->roots, path))) {
      continue;
    }

    si_pkg_info_t* manifest = sp_alloc_type(session->mem, si_pkg_info_t);
    spn_err_union_t err = sp_zero;
    if (spn_pkg_load(session->mem, session->ctx->intern, &session->ctx->roots, sp_intern_find(session->ctx->intern, pkg->id.qualified), path, manifest, &err)) {
      result = spn_err_emit(session->ctx, err);
      continue;
    }

    pkg->origin.recipe = (spn_pkg_root_t) { .kind = SPN_PKG_ROOT_LOCAL, .local = patch };
    pkg->origin.source = manifest->upstream;
    pkg->origin.manifest = manifest;
    pkg->name = manifest->name;
    pkg->options = manifest->options;
  }
  return result;
}

static void add_root(spn_session_t* session, spn_resolve_query_t* query) {
  spn_resolve_query_add(query, (spn_requested_dep_t) {
    .qualified = session->manifest->qualified,
    .source = SPN_PKG_SOURCE_ROOT,
  });
}

static void emit_resolved(spn_resolve_query_t* query) {
  sp_ht_for_kv(query->result, it) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_RESOLVE_PACKAGE,
      .resolve_pkg = {
        .name = spn_intern_find(it.val->id.qualified),
        .version = spn_semver_to_str(spn.events->mem, it.val->id.version),
      }
    });
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_RESOLVE_END,
    .resolve_end = {
      .num_resolved = sp_ht_size(query->result),
      .time = query->time,
    }
  });
}

spn_err_t resolve(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_RESOLVE_START,
    .pkg = session->manifest->name.str,
  });

  sp_ht_insert(session->registry, ((spn_pkg_id_t) { .qualified = session->manifest->qualified.id }), ((spn_registry_pkg_t) {
    .source = SPN_PKG_SOURCE_ROOT,
    .manifest = session->manifest,
    .path = session->project->paths.manifest,
  }));

  spn_index_cache_t index = sp_zero;
  spn_index_cache_init(&index, session->mem, session->ctx->intern, &session->ctx->roots, &spn.indexes);

  spn_resolver_t resolver = sp_zero;
  spn_resolver_init(&resolver, spn.mem, session->ctx->intern, &index, &session->registry, session->units.target->profile, session->project->config, 0);
  resolver.roots = &session->ctx->roots;
  resolver.seeds = session->gates.seeds;

  spn_resolve_query_t query = sp_zero_initialize();
  spn_resolve_query_init(session->mem, &query);
  add_root(session, &query);

  if (spn_resolve_from_solver(&resolver, &query)) {
    sp_da_for(query.errors, it) {
      spn_err_emit(session->ctx, query.errors[it]);
    }
    return query.errors[0].kind;
  }

  if (!spn_path_empty(session->ctx->paths.patches)) {
    spn_try(apply_patch_overrides(session, &query));
  }
  session->resolve = query.result;

  emit_resolved(&query);

  return SPN_OK;
}
