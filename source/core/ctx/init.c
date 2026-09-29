#include "spn/host.h"

#include "config.gen.h"
#include "codegen/lower.h"
#include "ctx/ctx.h"
#include "error/error.h"
#include "event/event.h"
#include "external/wasm/wasm.h"
#include "git/cache.h"
#include "hash/digest/digest.h"
#include "index/index.h"
#include "intern/intern.h"
#include "lazy/lazy.h"
#include "op/op.h"
#include "paths/paths.h"
#include "project/project.h"
#include "project/types.h"
#include "session/session.h"
#include "fs/fs.h"
#include "os/os.h"
#include "sp/sp_glob.h"
#include "spn.embed.h"
#include "toml/issue.h"
#include "toml/loader.h"
#include "toolchain/catalog.h"
#include "toolchain/probe.h"
#include "toolchain/provision.h"
#include "toolchain/sdk.h"
#include "triple/triple.h"
#include "version/version.h"

static sp_str_t env_or(spn_ctx_t* ctx, const c8* env, sp_str_t fallback) {
  sp_str_t path = sp_env_get(ctx->env, sp_cstr_as_str(env));
  return sp_str_empty(path) ? fallback : path;
}

static bool write_file(sp_path_t path, const void* data, u64 size) {
  sp_io_file_writer_t io = sp_zero;
  if (sp_io_file_writer_from_path_at(&io, path) != SP_OK) {
    return false;
  }
  sp_err_t written = sp_io_write(&io.base, data, size, SP_NULLPTR);
  sp_err_t closed = sp_io_file_writer_close(&io);
  return written == SP_OK && closed == SP_OK;
}

static spn_path_t storage_path(const c8* sub) {
  return (spn_path_t) { .root = SPN_PATH_ROOT_STORAGE, .sub = sp_cstr_as_str(sub) };
}

static sp_str_t read_stamp(sp_mem_t mem, spn_ctx_t* ctx) {
  sp_str_t version = sp_zero;
  sp_io_read_file_at(mem, spn_path_at(&ctx->roots, storage_path("runtime/version.stamp")), &version);
  return sp_str_trim(version);
}

static spn_err_t extract(spn_ctx_t* ctx, sp_mem_t mem, sp_str_t stamp) {
  spn_path_t runtime = storage_path("runtime");
  sp_path_t staging = sp_zero;
  if (sp_fs_staging_dir(mem, spn_path_at(&ctx->roots, runtime), sp_str_lit("tmp"), &staging) != SP_OK) {
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = runtime } });
  }

  sp_glob_set_t* glob = sp_glob_set_new(mem);
  sp_glob_set_add(glob, "include/*");
  sp_glob_set_build(glob);

  sp_carr_for(spn_embed_manifest, it) {
    spn_embed_entry_t entry = spn_embed_manifest[it];
    sp_str_t rel = sp_cstr_as_str(entry.path);
    if (!sp_glob_set_match(glob, rel)) {
      continue;
    }
    sp_path_t path = sp_path_join(mem, staging, rel);
    sp_fs_create_dir_at(sp_path_parent(mem, path));
    if (!write_file(path, entry.data, entry.size)) {
      sp_fs_remove_dir_at(staging);
      return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = { .root = SPN_PATH_ROOT_STORAGE, .sub = sp_str_copy(ctx->heap, path.sub) } } });
    }
  }

  sp_path_t stamp_path = sp_path_join(mem, staging, sp_str_lit("version.stamp"));
  if (!write_file(stamp_path, stamp.data, stamp.len)) {
    sp_fs_remove_dir_at(staging);
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = { .root = SPN_PATH_ROOT_STORAGE, .sub = sp_str_copy(ctx->heap, stamp_path.sub) } } });
  }

  sp_fs_remove_dir_at(spn_path_at(&ctx->roots, runtime));
  if (sp_sys_rename_s(staging.dir, staging.sub, staging.dir, runtime.sub)) {
    sp_fs_remove_dir_at(staging);
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = runtime } });
  }

  return SPN_OK;
}

static spn_err_t extract_runtime(spn_ctx_t* ctx) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t result = SPN_OK;

  // The stamp must change whenever the embedded runtime does, not just on
  // release; otherwise dev builds compile scripts against a stale extraction
  sp_hash_t runtime_hash = 0;
  sp_carr_for(spn_embed_manifest, it) {
    spn_embed_entry_t entry = spn_embed_manifest[it];
    sp_hash_t hashes [] = {
      runtime_hash,
      spn_digest_hash_str(sp_cstr_as_str(entry.path)),
      spn_digest_hash(entry.data, entry.size),
    };
    runtime_hash = spn_digest_hash_combine(hashes, sp_carr_len(hashes));
  }
  sp_str_t stamp = sp_fmt(scratch.mem, "{}:{}", sp_fmt_cstr(SPN_VERSION), sp_fmt_uint(runtime_hash)).value;

  if (!sp_str_equal(read_stamp(scratch.mem, ctx), stamp)) {
    sp_fs_lock_t lock = sp_zero;
    spn_path_t lock_path = storage_path("runtime.lock");
    if (sp_fs_lock_acquire(&lock, spn_path_at(&ctx->roots, lock_path)) != SP_OK) {
      result = spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = lock_path } });
    }
    else {
      if (!sp_str_equal(read_stamp(scratch.mem, ctx), stamp)) {
        result = extract(ctx, scratch.mem, stamp);
      }
      sp_fs_lock_release(&lock);
    }
  }

  sp_mem_end_scratch(scratch);
  return result;
}

static spn_err_t open_ctx(spn_ctx_t* ctx, spn_open_request_t request) {
  sp_assert(!ctx->config.indexes);

  sp_path_t project = sp_str_valid(request.dir) ? sp_path_resolve(request.dir) : sp_path_at_cwd(sp_str_lit("."));
  spn_path_roots_set(&ctx->roots, ctx->heap, SPN_PATH_ROOT_PROJECT, project);

  spn_git_cache_init(&ctx->caches.git, ctx->mem, ctx->intern, sp_fs_join_path(ctx->heap, ctx->roots.dirs[SPN_PATH_ROOT_CACHE], sp_str_lit("source")));

  ctx->caches.toolchains = (spn_toolchain_store_t) {
    .mem = ctx->mem,
    .dir = ctx->roots.dirs[SPN_PATH_ROOT_TOOLCHAIN],
    .mirror = sp_env_get(ctx->env, sp_str_lit("SPN_MIRROR")),
    .fetch = spn_fetch_curl,
  };
  spn_probe_cache_load(&ctx->caches.toolchains.probes, sp_fs_join_path(ctx->heap, ctx->roots.dirs[SPN_PATH_ROOT_TOOLCHAIN], sp_str_lit("probe.cache")), ctx->mem);

  spn_try(extract_runtime(ctx));
  spn_path_roots_set(&ctx->roots, ctx->heap, SPN_PATH_ROOT_RUNTIME, spn_path_at(&ctx->roots, storage_path("runtime")));

  spn_event_buffer_push(ctx->events, (spn_event_t) {
    .kind = SPN_EVENT_OPEN,
    .open = {
      .version = sp_cstr_as_str(SPN_VERSION),
      .project = ctx->roots.dirs[SPN_PATH_ROOT_PROJECT],
    },
  });

  // Load the per-machine config file
  ctx->config.indexes = sp_da_new(ctx->heap, spn_index_info_t);
  sp_path_t config_toml = sp_path_resolve(ctx->paths.config.toml);
  if (sp_fs_exists_at(config_toml)) {
    spn_cg_config_t config = sp_zero;
    spn_toml_loader_t loader = sp_zero;
    spn_toml_loader_init(&loader, ctx->mem, ctx->intern);
    sp_da(spn_index_info_t) indexes = sp_da_new(ctx->heap, spn_index_info_t);
    sp_da(spn_toolchain_decl_t) toolchains = SP_NULLPTR;
    if (spn_codegen_load_config(&loader, config_toml, &config) == SPN_OK) {
      sp_da_for(config.index, it) {
        sp_da_push(indexes, spn_index_lower(&loader, it, SPN_INDEX_KIND_USER, &config.index[it]));
      }
      toolchains = spn_toolchains_lower_list(&loader, SPN_PATH_ROOT_NONE, config.toolchain);
    }
    if (!sp_da_empty(loader.issues)) {
      return spn_err_emit(ctx, (spn_err_union_t) {
        .kind = SPN_ERR_MANIFEST_ISSUES,
        .manifest = { .path = ctx->paths.config.toml, .issues = spn_codegen_issues_to_err(ctx->mem, loader.issues) },
      });
    }
    ctx->config.indexes = indexes;
    sp_da_for(toolchains, it) {
      spn_toolchain_catalog_add(&ctx->catalog, toolchains[it]);
    }
  }

  spn_try(spn_project_load(ctx, &ctx->project));
  if (!request.project_optional) {
    spn_try(spn_ctx_require_project(ctx));
  }

  if (ctx->project) {
    sp_str_om_for(ctx->project->package.toolchains, it) {
      spn_toolchain_catalog_add(&ctx->catalog, *sp_str_om_at(ctx->project->package.toolchains, it));
    }
  }

  spn_index_assemble(ctx->heap, ctx->project ? &ctx->project->package.indexes : SP_NULLPTR, ctx->config.indexes, &ctx->indexes);

  sp_da_for(ctx->indexes, it) {
    spn_index_info_t* index = &ctx->indexes[it];
    index->location = spn_index_location(index, ctx->heap, ctx->roots.dirs[SPN_PATH_ROOT_INDEX]);
    if (!index->refresh) {
      index->refresh = request.index_refresh_seconds ? request.index_refresh_seconds : SPN_INDEX_DEFAULT_REFRESH;
    }
  }

  return SPN_OK;
}

static void load_builtins(spn_ctx_t* ctx) {
  spn_toml_loader_t loader = sp_zero;
  spn_toml_loader_init(&loader, ctx->mem, ctx->intern);
  sp_da(spn_toolchain_decl_t) decls = spn_toolchains_lower(&loader, sp_str((const c8*)toolchains_toml, toolchains_toml_size), SPN_PATH_ROOT_NONE);
  sp_assert(sp_da_empty(loader.issues));
  sp_da_for(decls, it) {
    spn_toolchain_catalog_add(&ctx->catalog, decls[it]);
  }
}

static void set_root(spn_ctx_t* ctx, spn_path_root_t kind, spn_path_root_t base, const c8* sub) {
  spn_path_t dir = { .root = base, .sub = sp_cstr_as_str(sub) };
  spn_path_roots_set(&ctx->roots, ctx->heap, kind, spn_path_at(&ctx->roots, dir));
}

spn_ctx_t* spn_ctx_new(spn_wake_fn_t wake, void* wake_data) {
  sp_assert(!spn.arena);
  spn_ctx_t* ctx = &spn;
  *ctx = sp_zero_s(spn_ctx_t);
  ctx->wake.fn = wake;
  ctx->wake.data = wake_data;
  ctx->mem = sp_mem_os_new();
  ctx->arena = sp_mem_arena_new(ctx->mem);
  ctx->heap = sp_mem_arena_as_allocator(ctx->arena);
  ctx->intern = sp_intern_new(ctx->mem);
  ctx->env = sp_alloc_type(ctx->heap, sp_env_t);
  *ctx->env = sp_env_capture(ctx->heap);
  ctx->events = spn_event_buffer_new(ctx->mem);
  ctx->events->wake = &ctx->wake;

  ctx->host = spn_triple_host();

  ctx->paths.patches = sp_env_get(ctx->env, sp_str_lit("SPN_PATCH_DIR"));
  ctx->paths.config.dir = sp_fs_join_path(ctx->heap, env_or(ctx, "SPN_CONFIG_DIR", sp_fs_get_config_path(ctx->heap)), sp_str_lit("spn"));
  ctx->paths.config.toml = sp_fs_join_path(ctx->heap, ctx->paths.config.dir, sp_str_lit("spn.toml"));

  sp_str_t storage = env_or(ctx, "SPN_STORAGE_DIR", sp_fs_join_path(ctx->heap, sp_fs_get_storage_path(ctx->heap), sp_str_lit("spn")));
  spn_path_roots_set(&ctx->roots, ctx->heap, SPN_PATH_ROOT_STORAGE, sp_path_resolve(storage));
  set_root(ctx, SPN_PATH_ROOT_CACHE, SPN_PATH_ROOT_STORAGE, "cache");
  set_root(ctx, SPN_PATH_ROOT_INDEX, SPN_PATH_ROOT_STORAGE, "index");
  set_root(ctx, SPN_PATH_ROOT_STORE, SPN_PATH_ROOT_CACHE, "store");
  set_root(ctx, SPN_PATH_ROOT_BUILD, SPN_PATH_ROOT_CACHE, "build");
  set_root(ctx, SPN_PATH_ROOT_CHECKOUT, SPN_PATH_ROOT_CACHE, "source/checkouts");
  sp_str_t toolchain = sp_env_get(ctx->env, sp_str_lit("SPN_TOOLCHAIN_DIR"));
  if (sp_str_empty(toolchain)) {
    set_root(ctx, SPN_PATH_ROOT_TOOLCHAIN, SPN_PATH_ROOT_CACHE, "toolchain");
  }
  else {
    spn_path_roots_set(&ctx->roots, ctx->heap, SPN_PATH_ROOT_TOOLCHAIN, sp_path_resolve(toolchain));
  }

  spn_toolchain_catalog_init(&ctx->catalog, ctx->host, spn_sdk_detect(ctx->heap, &ctx->roots, ctx->env, ctx->host), ctx->heap);
  load_builtins(ctx);
  ctx->roots.pinned = spn_path_pinned_roots();

  spn_op_thread_start(ctx);
  return ctx;
}

static spn_err_t open_session(spn_ctx_t* ctx, spn_session_config_t config) {
  spn_try(spn_ctx_require_project(ctx));

  ctx->session = sp_alloc_type(ctx->heap, spn_session_t);
  return spn_session_init(ctx->session, ctx, ctx->heap, ctx->project, config);
}

spn_err_t spn_ctx_open(spn_ctx_t* ctx, spn_open_request_t request) {
  return open_ctx(ctx, request);
}

spn_err_t spn_ctx_open_session(spn_ctx_t* ctx, const spn_session_config_t* config, spn_session_t** session) {
  spn_err_t err = open_session(ctx, *config);
  *session = err ? SP_NULLPTR : ctx->session;
  return err;
}

void spn_ctx_close(spn_ctx_t* ctx, bool ok) {
  spn_op_thread_stop(ctx);

  spn_err_t result = SPN_OK;
  if (!ok) {
    result = (spn_err_t)sp_atomic_s32_load(&ctx->error, SP_ATOMIC_SEQ_CST);
    if (!result) {
      result = SPN_ERROR;
    }
  }

  spn_event_buffer_push(ctx->events, (spn_event_t) {
    .kind = SPN_EVENT_RESULT,
    .result = {
      .ok = result == SPN_OK,
      .err = result,
    },
  });

  if (ctx->session) {
    sp_om_for(ctx->session->units.packages, it) {
      spn_pkg_unit_t* unit = sp_om_at(ctx->session->units.packages, it);
      spn_wasm_script_close(&unit->wasm.configure);
      spn_wasm_script_close(&unit->wasm.build);
    }
  }
  ctx->session = SP_NULLPTR;

  spn_lazy_log_close(&ctx->events->log);
  spn_path_roots_close(&ctx->roots);
}
