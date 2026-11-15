#include "model/resolve.h"

#include "sp.h"
#include "macro/macro.h"
#include "ctx/types.h"
#include "intern/types.h"
#include "project/types.h"
#include "resolve/types.h"

#include "error/error.h"
#include "event/event.h"
#include "index/cache.h"
#include "op/op.h"
#include "op/types.h"
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

    spn_path_t patch = spn_path_join(spn.mem, session->ctx->paths.patches, pkg->name.str);
    spn_path_t path = spn_path_join(spn.mem, patch, pkg->origin.paths.manifest);
    sp_sys_file_meta_t meta = sp_zero;
    if (sp_fs_get_path_metadata_at(spn_path_at(&session->ctx->roots, path), &meta) || meta.kind == SP_FS_KIND_UNKNOWN) {
      continue;
    }

    si_pkg_info_t* manifest = sp_alloc_type(session->mem, si_pkg_info_t);
    spn_err_union_t err = sp_zero;
    if (spn_pkg_load(session->mem, session->ctx->intern, &session->ctx->roots, path, manifest, &err)) {
      result = spn_err_emit(session->ctx, spn_err_about(err, pkg->name.str));
      continue;
    }

    pkg->origin.recipe = (spn_pkg_root_t) { .kind = SPN_PKG_ROOT_LOCAL, .local = patch };
    pkg->origin.source = manifest->upstream;
    pkg->origin.manifest = manifest;
    pkg->options.decls = manifest->options;
  }
  return result;
}

static void add_root(spn_session_t* session, spn_resolve_query_t* query) {
  spn_resolve_query_add(query, (spn_requested_dep_t) {
    .name = session->manifest->name,
    .source = SPN_PKG_SOURCE_ROOT,
  });
}

static void emit_resolved(spn_resolve_query_t* query) {
  sp_ht_for_kv(query->result, it) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_RESOLVE_PACKAGE,
      .resolve_pkg = {
        .name = it.val->name.str,
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

spn_err_t spn_resolve(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_RESOLVE_START,
    .pkg = session->manifest->name.str,
  });

  sp_ht_insert(session->registry, ((spn_pkg_id_t) { .name = session->manifest->name.id }), ((spn_registry_pkg_t) {
    .source = SPN_PKG_SOURCE_ROOT,
    .manifest = session->manifest,
    .path = session->project->paths.manifest,
  }));

  spn_ctx_t* ctx = session->ctx;
  bool* synced = sp_alloc_n(session->mem, bool, sp_da_size(ctx->indexes));
  spn_resolve_query_t query = sp_zero_initialize();
  spn_err_t err = SPN_OK;
  while (true) {
    spn_index_cache_t index = sp_zero;
    spn_index_cache_init(&index, session->mem, ctx->intern, &ctx->roots, &ctx->indexes);

    spn_resolver_t resolver = sp_zero;
    spn_resolver_init(&resolver, spn.mem, ctx->intern, &index, &session->registry, session->builds.program->profile, session->project->config, 0);
    resolver.roots = &ctx->roots;
    resolver.seeds = session->gates.seeds;

    spn_resolve_query_init(session->mem, &query);
    add_root(session, &query);
    err = spn_resolve_from_solver(&resolver, &query);

    sp_da(spn_index_info_t*) read = sp_da_new(session->mem, spn_index_info_t*);
    sp_for(it, index.consulted) {
      if (!synced[it]) {
        synced[it] = true;
        sp_da_push(read, &ctx->indexes[it]);
      }
    }
    if (sp_da_empty(read)) {
      break;
    }
    spn_try(spn_sync_index_list(op, read, false));
  }

  if (err) {
    sp_da_for(query.errors, it) {
      spn_err_emit(ctx, query.errors[it]);
    }
    return query.errors[0].kind;
  }

  if (!spn_path_empty(ctx->paths.patches)) {
    spn_try(apply_patch_overrides(session, &query));
  }
  session->resolve = query.result;
  session->root = sp_ht_getp(session->resolve, query.root);

  emit_resolved(&query);

  return SPN_OK;
}
