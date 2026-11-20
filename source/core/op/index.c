#include "sp.h"
#include "macro/macro.h"

#include "cpu/cpu.h"
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
#include "external/wasm/wasm.h"
#include "git/url.h"
#include "index/index.h"
#include "op/op.h"
#include "paths/paths.h"
#include "project/types.h"
#include "thread_pool/thread_pool.h"
#include "toml/edit.h"

typedef struct {
  spn_op_t* op;
  spn_index_info_t* index;
  spn_index_plan_t plan;
  spn_err_t err;
  spn_git_failure_t failure;
} job_t;

static void sync_index_node(void* data) {
  job_t* job = (job_t*)data;
  if (spn_op_cancelled(job->op)) {
    return;
  }
  job->err = spn_index_sync(job->index, job->plan, &job->op->cancelled, &job->failure);
}

spn_err_t si_sync_index_list(spn_op_t* op, sp_da(spn_index_info_t*) indexes, bool force) {
  spn_ctx_t* ctx = op->ctx;
  sp_da(job_t*) jobs = sp_da_new(ctx->mem, job_t*);

  sp_da_for(indexes, it) {
    spn_index_info_t* index = indexes[it];
    if (spn_index_is_snapshot(index)) {
      continue;
    }
    spn_index_plan_t plan = spn_index_plan(index, force);
    if (plan.fetch) {
      spn_event_buffer_push(ctx->events, (spn_event_t) {
        .kind = SPN_EVENT_SYNC,
        .sync = {
          .name = index->name.str,
          .url = spn_index_source(index),
        }});
    }

    job_t* job = sp_alloc_type(ctx->mem, job_t);
    *job = (job_t) {
      .op = op,
      .index = index,
      .plan = plan,
    };
    sp_da_push(jobs, job);
  }

  spn_thread_pool_t pool = sp_zero;
  spn_thread_pool_init(&pool, ctx->mem, (spn_thread_pool_config_t) {
    .workers = sp_min(spn_cpu_count(), (u32)sp_da_size(jobs)),
    .on_worker_exit = spn_wasm_thread_exit,
  });

  sp_da_for(jobs, it) {
    spn_thread_pool_submit(&pool, (spn_thread_pool_job_t) { .fn = sync_index_node, .data = jobs[it] });
  }

  spn_thread_pool_wait(&pool);
  spn_thread_pool_deinit(&pool);

  if (spn_op_cancelled(op)) {
    return SPN_ERR_CANCELLED;
  }

  sp_da_for(jobs, it) {
    job_t* job = jobs[it];
    if (job->err == SPN_OK) {
      continue;
    }

    if (!job->plan.force && job->plan.present) {
      spn_event_buffer_push(ctx->events, (spn_event_t) {
        .kind = SPN_EVENT_SYNC_STALE,
        .sync = {
          .name = job->index->name.str,
          .url = spn_index_source(job->index),
        }});
      continue;
    }

    if (job->failure.kind == SPN_GIT_UNTRUSTED) {
      return spn_err_emit(ctx, (spn_err_union_t) {
        .kind = SPN_ERR_CERT_UNTRUSTED,
        .subject = job->index->name.str,
        .cert = {
          .url = spn_index_source(job->index),
          .trust = ctx->trust,
          .config = spn_path_str(&ctx->roots, ctx->events->mem, ctx->paths.config),
          .cause = sp_str_copy(ctx->events->mem, sp_cstr_as_str(job->failure.message)),
          .git = true,
        }});
    }

    if (job->failure.remote) {
      return spn_err_emit(ctx, (spn_err_union_t) {
        .kind = SPN_ERR_GIT_FETCH,
        .subject = job->index->name.str,
        .git_fetch = {
          .url = spn_index_source(job->index),
          .config = spn_path_str(&ctx->roots, ctx->events->mem, ctx->paths.config),
          .cause = sp_str_copy(ctx->events->mem, sp_cstr_as_str(job->failure.message)),
          .route = job->failure.route,
        }});
    }

    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_SYNC,
      .index = {
        .name = job->index->name.str,
        .url = spn_index_source(job->index),
        .cause = sp_str_copy(ctx->events->mem, sp_cstr_as_str(job->failure.message)),
      }});
  }

  return SPN_OK;
}

static spn_path_t declared_in(spn_ctx_t* ctx, spn_index_info_t* index) {
  switch (index->kind) {
    case SPN_INDEX_KIND_WORKSPACE: {
      return ctx->project->paths.manifest;
    }
    case SPN_INDEX_KIND_USER: {
      return ctx->paths.config;
    }
  }
  sp_unreachable_return(sp_zero_s(spn_path_t));
}

static bool is_declared(spn_ctx_t* ctx, bool global, spn_index_id_t id) {
  if (global) {
    return spn_index_find(&ctx->config.indexes, id) != SP_NULLPTR;
  }
  return si_om_get(ctx->project->indexes, id) != SP_NULLPTR;
}

static spn_err_t declare(spn_ctx_t* ctx, spn_index_info_t* index) {
  spn_path_t declared = declared_in(ctx, index);
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
    sp_io_write_cstr(&out.base, "\nformat = ", SP_NULLPTR);
    spn_toml_write_str(&out.base, spn_index_format_to_str(index->format));
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

spn_err_t spn_op_add_index(spn_op_t* op) {
  spn_ctx_t* ctx = op->ctx;
  spn_index_add_request_t request = op->request.index;

  sp_intern_str_t name = sp_intern(ctx->intern, sp_str_empty(request.name) ? spn_git_url_name(request.url) : request.name);
  if (is_declared(ctx, request.global, name.id)) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_EXISTS,
      .index = { .name = name.str, .url = request.url },
    });
  }

  spn_index_info_t index = {
    .id = name.id,
    .name = name,
    .kind = request.global ? SPN_INDEX_KIND_USER : SPN_INDEX_KIND_WORKSPACE,
    .protocol = SPN_INDEX_PROTOCOL_GIT,
    .git = { .url = request.url },
    .refresh = SPN_INDEX_DEFAULT_REFRESH,
    .remote = ctx->caches.git.remote,
    .backoff = spn_net_default_backoff,
  };
  si_index_locate(&index, ctx->heap, &ctx->roots, spn_path_from_id(SPN_DIR_ID_INDEX));

  sp_da(spn_index_info_t*) fetched = sp_da_new(ctx->mem, spn_index_info_t*);
  sp_da_push(fetched, &index);
  spn_try(si_sync_index_list(op, fetched, true));

  index.format = spn_index_detect_format(&index);
  if (request.global && spn_index_is_snapshot(&index) && spn_git_get_commit_full(ctx->heap, index.location.dir, sp_str_lit("HEAD"), &index.git.rev)) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_SYNC,
      .index = { .name = name.str, .url = request.url },
    });
  }
  spn_try(declare(ctx, &index));
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
  return si_sync_index_list(op, indexes, request.force);
}

spn_op_t* spn_sync_indexes(spn_ctx_t* ctx, spn_sync_request_t request) {
  spn_op_t* op = spn_op_new(ctx, SP_NULLPTR, SPN_OP_SYNC_INDEXES);
  op->request.indexes = (spn_sync_request_t) {
    .force = request.force,
    .only = sp_str_copy(ctx->heap, request.only),
  };
  spn_op_submit(op);
  return op;
}
