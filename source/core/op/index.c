#include "sp.h"
#include "macro/macro.h"

#include "ctx/ctx.h"
#include "ctx/types.h"
#include "error/error.h"
#include "spn/errors.h"
#include "event/types.h"
#include "core/types.h"
#include "index/types.h"
#include "intern/intern.h"
#include "spn/host.h"

#include "event/event.h"
#include "git/cache.h"
#include "git/remote.h"
#include "git/url.h"
#include "index/index.h"
#include "index/tip.h"
#include "lock/lock.h"
#include "op/op.h"
#include "paths/paths.h"
#include "project/types.h"
#include "toml/edit.h"

static spn_err_t report(spn_ctx_t* ctx, spn_index_info_t* index, const spn_git_failure_t* failure) {
  if (failure->kind == SPN_GIT_CANCELLED) {
    return SPN_ERR_CANCELLED;
  }

  sp_str_t cause = sp_str_copy(ctx->events->mem, sp_cstr_as_str(failure->message));
  if (failure->kind == SPN_GIT_UNTRUSTED) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_CERT_UNTRUSTED,
      .subject = index->name.str,
      .cert = {
        .url = spn_index_source(index),
        .trust = ctx->trust,
        .config = spn_path_str(&ctx->roots, ctx->events->mem, ctx->paths.config),
        .cause = cause,
        .git = true,
      }});
  }
  if (failure->remote) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_GIT_FETCH,
      .subject = index->name.str,
      .git_fetch = {
        .url = spn_index_source(index),
        .config = spn_path_str(&ctx->roots, ctx->events->mem, ctx->paths.config),
        .cause = cause,
        .route = failure->route,
      }});
  }
  return spn_err_emit(ctx, (spn_err_union_t) {
    .kind = SPN_ERR_INDEX_SYNC,
    .index = {
      .name = index->name.str,
      .url = spn_index_source(index),
      .cause = cause,
    }});
}

static spn_err_t read_marker(spn_ctx_t* ctx, sp_mem_t mem, spn_index_info_t* index, spn_path_t dir, si_index_marker_t* marker) {
  spn_err_union_t unread = sp_zero;
  if (si_index_read_marker(index, mem, ctx->intern, &ctx->roots, dir, marker, &unread)) {
    return spn_err_emit(ctx, unread);
  }
  return SPN_OK;
}

static spn_err_t refresh(spn_op_t* op, sp_mem_t mem, spn_index_info_t* index, sp_str_t* rev, spn_git_failure_t* failure) {
  spn_git_cache_t* git = &op->ctx->caches.git;
  sp_str_t tip = sp_zero;
  spn_err_t err = si_git_tip(index->remote, mem, index->git.url, index->backoff, &op->cancelled, &tip, failure);
  if (!err) {
    bool fetching = false;
    spn_git_checkout_t* checkout = spn_git_cache_checkout(git, (spn_git_checkout_id_t) { .url = index->git.url, .rev = tip }, &fetching);
    err = spn_git_checkout_ensure(git, checkout, &op->cancelled);
    *failure = checkout->failure;
  }

  if (!err) {
    si_index_tip_save(index, tip);
    *rev = tip;
  }
  else if (failure->kind != SPN_GIT_CANCELLED) {
    si_index_tip_hold(index);
  }
  return err;
}

spn_err_t si_place_index(spn_op_t* op, sp_mem_t mem, const spn_lock_t* held, spn_index_info_t* index, si_index_place_t* place) {
  spn_ctx_t* ctx = op->ctx;
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      sp_str_t rev = si_index_pin(index, held);
      if (sp_str_empty(rev)) {
        si_index_tip_t tip = si_index_tip_plan(mem, index);
        rev = tip.rev;
        if (tip.fetch) {
          spn_event_buffer_push(ctx->events, (spn_event_t) {
            .kind = SPN_EVENT_SYNC,
            .sync = { .name = index->name.str, .url = index->git.url },
          });

          spn_git_failure_t failure = sp_zero;
          if (refresh(op, mem, index, &rev, &failure)) {
            if (sp_str_empty(tip.rev) || failure.kind == SPN_GIT_CANCELLED) {
              return report(ctx, index, &failure);
            }
            tip.held = true;
          }
        }
        if (tip.held) {
          spn_event_buffer_push(ctx->events, (spn_event_t) {
            .kind = SPN_EVENT_SYNC_STALE,
            .sync = { .name = index->name.str, .url = index->git.url },
          });
        }
      }

      bool fetching = false;
      spn_git_checkout_t* checkout = spn_git_cache_checkout(&ctx->caches.git, (spn_git_checkout_id_t) { .url = index->git.url, .rev = rev }, &fetching);
      if (fetching) {
        spn_event_buffer_push(ctx->events, (spn_event_t) {
          .kind = SPN_EVENT_SYNC,
          .sync = { .name = index->name.str, .url = index->git.url },
        });
      }
      if (spn_git_checkout_ensure(&ctx->caches.git, checkout, &op->cancelled)) {
        return report(ctx, index, &checkout->failure);
      }

      si_index_marker_t marker = sp_zero;
      spn_try(read_marker(ctx, mem, index, checkout->path, &marker));

      *place = (si_index_place_t) {
        .format = marker.format,
        .at = sp_path_join(mem, spn_path_at(&ctx->roots, checkout->path), marker.path),
        .dir = sp_fs_join_path(mem, spn_path_str(&ctx->roots, mem, checkout->path), marker.path),
      };
      if (marker.format == SPN_INDEX_FORMAT_MANIFESTS) {
        if (index->kind == SPN_INDEX_KIND_USER && sp_str_empty(index->git.rev)) {
          return spn_err_emit(ctx, (spn_err_union_t) {
            .kind = SPN_ERR_INDEX_UNPINNED,
            .index = { .name = index->name.str, .url = index->git.url },
          });
        }
        place->git.url = index->git.url;
        place->git.rev = rev;
        place->git.dir = marker.path;
      }
      break;
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      spn_git_failure_t unread = sp_zero;
      return report(ctx, index, &unread);
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      sp_sys_file_meta_t meta = sp_zero;
      if (sp_fs_get_link_metadata_at(index->location.at, &meta) || meta.kind != SP_FS_KIND_DIR) {
        spn_git_failure_t absent = sp_zero;
        return report(ctx, index, &absent);
      }

      si_index_marker_t marker = sp_zero;
      spn_try(read_marker(ctx, mem, index, (spn_path_t) { .sub = index->location.dir }, &marker));
      *place = (si_index_place_t) {
        .format = marker.format,
        .at = sp_path_join(mem, index->location.at, marker.path),
        .dir = sp_fs_join_path(mem, index->location.dir, marker.path),
      };
      break;
    }
  }
  return SPN_OK;
}

spn_err_t si_place_indexes(spn_op_t* op, sp_mem_t mem, const spn_lock_t* held, si_da(si_index_place_t) placed, u32 consulted, bool* grew) {
  *grew = false;
  sp_for(it, consulted) {
    if (!sp_str_empty(placed[it].dir)) {
      continue;
    }
    spn_try(si_place_index(op, mem, held, &op->ctx->indexes[it], &placed[it]));
    *grew = true;
  }
  return SPN_OK;
}

spn_err_t si_refresh_indexes(spn_op_t* op, sp_da(spn_index_info_t*) indexes) {
  spn_ctx_t* ctx = op->ctx;
  sp_da_for(indexes, it) {
    spn_index_info_t* index = indexes[it];
    if (index->protocol != SPN_INDEX_PROTOCOL_GIT || !sp_str_empty(index->git.rev)) {
      continue;
    }
    spn_event_buffer_push(ctx->events, (spn_event_t) {
      .kind = SPN_EVENT_SYNC,
      .sync = { .name = index->name.str, .url = index->git.url },
    });

    sp_str_t rev = sp_zero;
    spn_git_failure_t failure = sp_zero;
    if (refresh(op, op->mem, index, &rev, &failure)) {
      return report(ctx, index, &failure);
    }
  }
  return SPN_OK;
}

static spn_err_t declare(spn_ctx_t* ctx, spn_path_t declared, spn_index_info_t* index) {
  sp_path_t at = spn_path_at(&ctx->roots, declared);
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t result = SPN_OK;

  sp_str_t source = sp_zero;
  sp_sys_file_meta_t meta = sp_zero;
  bool exists = !sp_fs_get_path_metadata_at(at, &meta) && meta.kind != SP_FS_KIND_UNKNOWN;
  if (exists && sp_fs_read_at(scratch.mem, at, &source)) {
    result = spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_READ, .fs = { .path = declared } });
  }
  else {
    sp_io_dyn_mem_writer_t out = sp_zero;
    sp_io_dyn_mem_writer_init(scratch.mem, &out);
    sp_fmt_io(&out.base, "{}", sp_fmt_str(source));
    if (source.len && source.data[source.len - 1] != '\n') {
      sp_io_write_c8(&out.base, '\n');
    }
    if (source.len) {
      sp_io_write_c8(&out.base, '\n');
    }
    sp_io_write_cstr(&out.base, "[[index]]\nname = ", SP_NULLPTR);
    spn_toml_write_str(&out.base, index->name.str);
    sp_io_write_cstr(&out.base, "\nurl = ", SP_NULLPTR);
    spn_toml_write_str(&out.base, index->git.url);
    if (!sp_str_empty(index->git.rev)) {
      sp_io_write_cstr(&out.base, "\nrev = ", SP_NULLPTR);
      spn_toml_write_str(&out.base, index->git.rev);
    }
    sp_io_write_c8(&out.base, '\n');

    sp_fs_create_parent_at(at);
    if (sp_fs_write_at(at, sp_io_dyn_mem_writer_as_str(&out), SP_FS_WRITE_REPLACE, sp_sys_default_file_perms)) {
      result = spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = declared } });
    }
  }

  sp_mem_end_scratch(scratch);
  return result;
}

spn_err_t si_op_add_index(spn_op_t* op) {
  spn_ctx_t* ctx = op->ctx;
  spn_index_add_request_t request = op->request.index;

  sp_intern_str_t name = sp_intern(ctx->intern, sp_str_empty(request.name) ? spn_git_url_name(request.url) : request.name);
  bool exists = request.global
    ? spn_index_find(&ctx->config.indexes, name.id) != SP_NULLPTR
    : si_om_get(ctx->project->indexes, name.id) != SP_NULLPTR;
  if (exists) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_EXISTS,
      .index = { .name = name.str, .url = request.url },
    });
  }

  spn_index_info_t index = {
    .id = name.id,
    .name = name,
    .protocol = SPN_INDEX_PROTOCOL_GIT,
    .git = { .url = request.url },
    .refresh = SPN_INDEX_DEFAULT_REFRESH,
    .remote = ctx->caches.git.remote,
    .backoff = spn_net_default_backoff,
  };
  si_index_locate(&index, ctx->heap, &ctx->roots, spn_path_from_id(SPN_DIR_ID_INDEX));

  spn_lock_t unpinned = spn_lock_new(op->mem);
  si_index_place_t place = sp_zero;
  spn_try(si_place_index(op, op->mem, &unpinned, &index, &place));
  if (request.global) {
    index.git.rev = place.git.rev;
  }
  spn_try(declare(ctx, request.global ? ctx->paths.config : ctx->project->paths.manifest, &index));
  spn_event_buffer_push(ctx->events, (spn_event_t) {
    .kind = SPN_EVENT_INDEX_ADDED,
    .sync = { .name = name.str, .url = request.url },
  });
  return SPN_OK;
}

spn_op_t* spn_add_index(spn_ctx_t* ctx, spn_index_add_request_t request) {
  spn_op_t* op = spn_op_new(ctx, SP_NULLPTR, SPN_OP_ADD_INDEX);
  op->request.index = (spn_index_add_request_t) {
    .url = sp_str_copy(ctx->heap, request.url),
    .name = sp_str_copy(ctx->heap, request.name),
    .global = request.global,
  };
  spn_op_submit(op);
  return op;
}

spn_err_t si_op_sync_indexes(spn_op_t* op) {
  spn_ctx_t* ctx = op->ctx;
  spn_sync_request_t request = op->request.indexes;
  if (!sp_str_empty(request.only) && !spn_find_index(ctx, request.only)) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_UNKNOWN,
      .index = { .name = request.only },
    });
  }

  spn_index_id_t only = sp_intern(ctx->intern, request.only).id;
  sp_da(spn_index_info_t*) indexes = sp_da_new(ctx->mem, spn_index_info_t*);
  sp_da_for(ctx->indexes, it) {
    if (!sp_str_empty(request.only) && ctx->indexes[it].id != only) {
      continue;
    }
    sp_da_push(indexes, &ctx->indexes[it]);
  }
  return si_refresh_indexes(op, indexes);
}

spn_op_t* spn_sync_indexes(spn_ctx_t* ctx, spn_sync_request_t request) {
  spn_op_t* op = spn_op_new(ctx, SP_NULLPTR, SPN_OP_SYNC_INDEXES);
  op->request.indexes = (spn_sync_request_t) {
    .only = sp_str_copy(ctx->heap, request.only),
  };
  spn_op_submit(op);
  return op;
}
