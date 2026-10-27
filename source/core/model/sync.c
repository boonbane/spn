#include "sp.h"
#include "os/os.h"
#include "str/str.h"
#include "cpu/cpu.h"
#include "ctx/ctx.h"
#include "ctx/types.h"
#include "dag/glob.h"
#include "error/error.h"
#include "spn/errors.h"
#include "event/event.h"
#include "core/types.h"
#include "git/cache.h"
#include "hash/digest/digest.h"
#include "intern/intern.h"
#include "lazy/lazy.h"
#include "op/op.h"
#include "paths/paths.h"
#include "pkg/id.h"
#include "pkg/load.h"
#include "pkg/types.h"
#include "project/types.h"
#include "resolve/types.h"
#include "session/session.h"
#include "session/types.h"
#include "spn/core.h"
#include "thread_pool/thread_pool.h"
#include "toolchain/probe.h"
#include "toolchain/toolchain.h"
#include "toolchain/types.h"
#include "unit/types.h"
#include "unit/unit.h"
#include "when/when.h"
#include "external/wasm/wasm.h"

typedef struct {
  spn_op_t* op;
  spn_session_t* session;
  spn_resolved_pkg_t* pkg;
  spn_loaded_pkg_t loaded;
  bool patched;
  bool fetched;
  u64 elapsed;
  spn_err_t err;
} pkg_job_t;

typedef struct {
  spn_op_t* op;
  spn_toolchain_unit_t* unit;
  spn_err_t err;
} toolchain_job_t;

static spn_cc_t cc_toolchain(spn_toolchain_info_t* toolchain, spn_toolchain_launcher_t compiler, spn_toolchain_launcher_t cxx, spn_toolchain_launcher_t archiver) {
  return (spn_cc_t) {
    .name = toolchain->name,
    .driver = toolchain->driver,
    .compiler = compiler,
    .cxx = cxx,
    .archiver = archiver,
    .link_args = toolchain->link_args,
    .archiver_driver = toolchain->driver == SPN_CC_DRIVER_MSVC ? SPN_AR_DRIVER_MSVC : SPN_AR_DRIVER_GNU,
  };
}

static spn_err_t setup_local(spn_toolchain_store_t* store, spn_toolchain_unit_t* unit) {
  spn_toolchain_info_t* toolchain = unit->info;
  sp_tm_timer_t timer = sp_tm_start_timer();

  unit->cc = cc_toolchain(toolchain, toolchain->compiler, toolchain->cxx, toolchain->archiver);
  spn_try(spn_toolchain_probe(&unit->cc, store->roots, spn_search_rules(spn.host.os), sp_env_get(spn.env, sp_str_lit("PATH")), &store->probes, spn.mem, &unit->identity));
  spn_probe_cache_flush(&store->probes);

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SYNC_PACKAGE,
    .sync_pkg = {
      .name = toolchain->name.str,
      .time = sp_tm_read_timer(&timer),
    }
  });
  return SPN_OK;
}

static spn_err_t setup_artifact(spn_toolchain_store_t* store, spn_toolchain_unit_t* unit) {
  spn_toolchain_info_t* toolchain = unit->info;
  spn_artifact_t artifact = toolchain->support.artifact;
  sp_tm_timer_t timer = sp_tm_start_timer();

  sp_str_t url = spn_artifact_resolve_url(spn.events->mem, artifact, store->mirror);
  spn_path_t root = spn_toolchain_artifact_root(spn.mem, artifact);
  bool cached = sp_fs_is_dir_at(spn_path_at(store->roots, root));
  if (!cached) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_SYNC,
      .sync = {
        .name = toolchain->name.str,
        .url = url,
      }});
    spn_try(spn_toolchain_provision(store, toolchain->name.str, artifact));
  }

  spn_toolchain_launcher_t cxx = toolchain->cxx;
  if (spn_toolchain_has_cxx(toolchain)) {
    cxx = spn_toolchain_launcher_with_root(spn.mem, toolchain->cxx, root);
  }
  unit->version = toolchain->version;
  unit->identity = spn_digest_hash_str(artifact.sha256);
  unit->cc = cc_toolchain(
    toolchain,
    spn_toolchain_launcher_with_root(spn.mem, toolchain->compiler, root),
    cxx,
    spn_toolchain_launcher_with_root(spn.mem, toolchain->archiver, root)
  );

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SYNC_PACKAGE,
    .sync_pkg = {
      .name = toolchain->name.str,
      .url = url,
      .source_path = spn_path_str(store->roots, spn.events->mem, root),
      .time = sp_tm_read_timer(&timer),
      .fetched = !cached,
    }
  });
  return SPN_OK;
}

static spn_err_t setup_toolchain_unit(spn_toolchain_store_t* store, spn_toolchain_unit_t* unit) {
  switch (unit->info->support.kind) {
    case SPN_TOOLCHAIN_SUPPORT_LOCAL: {
      spn_try(setup_local(store, unit));
      break;
    }
    case SPN_TOOLCHAIN_SUPPORT_ARTIFACT: {
      spn_try(setup_artifact(store, unit));
      break;
    }
    case SPN_TOOLCHAIN_SUPPORT_DETECTED: {
      spn_toolchain_info_t* toolchain = unit->info;
      unit->cc = cc_toolchain(toolchain, toolchain->compiler, toolchain->cxx, toolchain->archiver);
      break;
    }
    case SPN_TOOLCHAIN_SUPPORT_NONE: {
      sp_unreachable_case();
    }
  }

  unit->cc.wasi = spn_toolchain_wasi_spelling(store->roots, spn.mem, unit->info);
  if (unit->info->driver == SPN_CC_DRIVER_ZIG) {
    sp_assert(unit->identity);
    sp_str_t id = sp_fmt(spn.mem, "{:0>16x}", sp_fmt_uint(unit->identity)).value;
    unit->cc.cache = spn_path_s(spn.mem, SPN_DIR_ID_TOOLCHAIN_EXTERNAL, sp_fmt(spn.mem, "zig/cache/{}", sp_fmt_str(id)).value);
    spn_try(spn_toolchain_generation(store->roots, unit->cc.cache, &unit->generation));
  }
  return SPN_OK;
}

static spn_err_t materialize_tree(spn_session_t* session, sp_str_t name, spn_pkg_root_t tree, spn_path_t* root, bool* fetched) {
  switch (tree.kind) {
    case SPN_PKG_ROOT_LOCAL: {
      spn_path_t canonical = spn_path_canonicalize(spn.mem, &session->ctx->roots, tree.local);
      if (!sp_fs_exists_at(spn_path_at(&session->ctx->roots, canonical))) {
        return spn_err_emit(session->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_NO_MANIFEST,
          .no_manifest = { .path = spn_path_str(&session->ctx->roots, spn.events->mem, tree.local) },
        });
      }
      *root = canonical;
      return SPN_OK;
    }
    case SPN_PKG_ROOT_GIT: {
      if (!spn_git_cache_is_checkout_cached(&session->ctx->caches.git, tree.git)) {
        spn_event_buffer_push(spn.events, (spn_event_t){
          .kind = SPN_EVENT_SYNC,
          .sync = {
            .name = name,
            .url = tree.git.url,
          }
        });
      }

      spn_git_checkout_t* checkout = SP_NULLPTR;
      if (spn_git_cache_ensure_checkout(&session->ctx->caches.git, tree.git, &checkout)) {
        sp_str_t error = sp_str_lit("failed to fetch repository");
        if (!sp_str_empty(checkout->error)) {
          error = checkout->error;
        }
        spn_event_buffer_push(spn.events, (spn_event_t) {
          .kind = SPN_EVENT_SYNC_FAILED,
          .sync_failed = {
              .name = name,
              .url = tree.git.url,
              .error = error,
          }
        });
        return SPN_ERROR;
      }

      *root = checkout->path;
      *fetched |= checkout->fetched;
      return SPN_OK;
    }
    case SPN_PKG_ROOT_NONE: {
      *root = sp_zero_struct(spn_path_t);
      return SPN_OK;
    }
  }

  sp_unreachable_return(SPN_ERROR);
}

static spn_err_t fetch_package(pkg_job_t* job) {
  spn_session_t* session = job->session;
  spn_resolved_pkg_t* pkg = job->pkg;
  spn_loaded_pkg_t* loaded = &job->loaded;
  bool* fetched = &job->fetched;
  sp_str_t qualified = spn_intern_find(pkg->id.qualified);

  if (job->patched && !spn_git_cache_is_checkout_cached(&session->ctx->caches.git, pkg->origin.source.git)) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_SYNC_PATCH,
      .sync = {
        .name = qualified,
        .url = pkg->origin.source.git.url,
      }});
  }

  spn_try(materialize_tree(session, qualified, pkg->origin.recipe, &loaded->roots.recipe, fetched));

  if (pkg->origin.source.kind == SPN_PKG_ROOT_NONE) {
    loaded->roots.source = loaded->roots.recipe;
  } else {
    spn_try(materialize_tree(session, qualified, pkg->origin.source, &loaded->roots.source, fetched));
  }

  return SPN_OK;
}

static sp_atomic_s32_t sync_failed;

static void sync_package_node(void *data) {
  pkg_job_t* job = (pkg_job_t*)data;
  if (sp_atomic_s32_load(&sync_failed, SP_ATOMIC_SEQ_CST) || spn_op_cancelled(job->op)) {
    return;
  }
  sp_tm_timer_t timer = sp_tm_start_timer();
  job->err = fetch_package(job);
  job->elapsed = sp_tm_read_timer(&timer);
  if (job->err) {
    sp_atomic_s32_store(&sync_failed, (s32)job->err, SP_ATOMIC_SEQ_CST);
  }
}

static void sync_toolchain_node(void* data) {
  toolchain_job_t* job = (toolchain_job_t*)data;
  if (sp_atomic_s32_load(&sync_failed, SP_ATOMIC_SEQ_CST) || spn_op_cancelled(job->op)) {
    return;
  }
  job->err = setup_toolchain_unit(&spn.caches.toolchains, job->unit);
  if (job->err) {
    sp_atomic_s32_store(&sync_failed, (s32)job->err, SP_ATOMIC_SEQ_CST);
  }
}

static spn_err_t sync_jobs(spn_op_t* op, sp_mem_t mem, bool* reresolve) {
  spn_session_t* session = op->session;
  si_da(pkg_job_t*) packages = sp_zero;
  si_da(toolchain_job_t*) toolchains = sp_zero;

  u32 num_index = 0;
  u32 num_file = 0;
  spn_err_t stamped = SPN_OK;
  sp_ht_for_kv(session->resolve, it) {
    spn_resolved_pkg_t *pkg = it.val;
    switch (pkg->source) {
      case SPN_PKG_SOURCE_ROOT: break;
      case SPN_PKG_SOURCE_INDEX: num_index++; break;
      case SPN_PKG_SOURCE_FILE: num_file++; break;
    }

    pkg_job_t* job = sp_alloc_type(mem, pkg_job_t);
    job->op = op;
    job->session = session;
    job->pkg = pkg;
    si_da_for_ptr(session->patches, patch) {
      if (patch->qualified.id != pkg->id.qualified) {
        continue;
      }
      if (pkg->origin.source.kind != SPN_PKG_ROOT_GIT) {
        spn_err_t kind = spn_err_emit(session->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_PATCH_NOT_GIT,
          .subject = patch->qualified.str,
        });
        if (!stamped) {
          stamped = kind;
        }
        continue;
      }
      pkg->origin.source.git.patches = patch->set;
      job->patched = true;
    }
    si_da_push(mem, packages, job);
  }
  spn_try(stamped);

  si_da_for_value(session->units.toolchains, unit) {
    toolchain_job_t* job = sp_alloc_type(mem, toolchain_job_t);
    job->op = op;
    job->unit = unit;
    si_da_push(mem, toolchains, job);
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SYNC_START,
    .sync_start = {
      .num_packages = si_da_size(packages),
      .num_index = num_index,
      .num_file = num_file,
    }});

  sp_atomic_s32_store(&sync_failed, 0, SP_ATOMIC_SEQ_CST);

  u32 num_jobs = (u32)(si_da_size(packages) + si_da_size(toolchains));
  spn_thread_pool_t pool = sp_zero;
  spn_thread_pool_init(&pool, spn.mem, (spn_thread_pool_config_t) {
    .workers = sp_min(spn_cpu_count(), num_jobs),
    .on_worker_exit = spn_wasm_thread_exit,
  });

  sp_tm_timer_t timer = sp_tm_start_timer();

  si_da_for_value(packages, job) {
    spn_thread_pool_submit(&pool.executor, (spn_thread_pool_job_t) { .fn = sync_package_node, .data = job });
  }
  si_da_for_value(toolchains, job) {
    spn_thread_pool_submit(&pool.executor, (spn_thread_pool_job_t) { .fn = sync_toolchain_node, .data = job });
  }

  spn_thread_pool_wait(&pool);
  spn_thread_pool_deinit(&pool);
  u64 elapsed = sp_tm_read_timer(&timer);

  if (spn_op_cancelled(op)) {
    return SPN_ERR_CANCELLED;
  }

  si_da_for_value(packages, job) {
    spn_try(job->err);
  }
  si_da_for_value(toolchains, job) {
    spn_try(job->err);
  }


  const spn_path_roots_t* roots = &session->ctx->roots;
  si_da_for_value(packages, job) {
    spn_resolved_pkg_t* pkg = job->pkg;
    spn_loaded_pkg_t* loaded = &job->loaded;
    sp_str_t qualified = spn_intern_find(pkg->id.qualified);

    loaded->source = pkg->source;
    loaded->manifest = pkg->origin.manifest;
    if (!loaded->manifest) {
      spn_path_t path = spn_path_join(spn.mem, loaded->roots.recipe, pkg->origin.paths.manifest);
      if (!sp_fs_exists_at(spn_path_at(roots, path))) {
        return spn_err_emit(session->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_NO_MANIFEST,
          .subject = qualified,
          .no_manifest = { .path = spn_path_str(roots, spn.events->mem, path) },
        });
      }
      si_pkg_info_t* parsed = sp_alloc_type(session->mem, si_pkg_info_t);
      spn_err_union_t err = sp_zero;
      if (spn_pkg_load(session->mem, session->ctx->intern, roots, path, parsed, &err)) {
        return spn_err_emit(session->ctx, spn_err_about(err, qualified));
      }

      // Short names only: published manifests routinely omit the namespace the
      // index assigns, but config keys and consumer routing use the name
      sp_str_t requested = spn_pkg_name_from_qualified(qualified).name;
      if (!sp_str_equal(parsed->name.str, requested)) {
        return spn_err_emit(session->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_PKG_MISMATCH,
          .mismatch = { .path = spn_path_str(roots, spn.events->mem, path), .declared = parsed->name.str, .requested = qualified },
        });
      }
      loaded->manifest = parsed;
    }

    pkg->name = loaded->manifest->name;
    pkg->options = loaded->manifest->options;
    sp_ht_insert(session->packages, pkg->id, job->loaded);

    sp_str_t url = sp_str_lit("");
    if (pkg->origin.recipe.kind == SPN_PKG_ROOT_GIT) {
      url = pkg->origin.recipe.git.url;
    }
    if (pkg->origin.source.kind == SPN_PKG_ROOT_GIT) {
      url = pkg->origin.source.git.url;
    }
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_SYNC_PACKAGE, .pkg = loaded->manifest->name.str,
      .sync_pkg = {
        .name = qualified,
        .url = url,
        .source_path = spn_path_str(roots, spn.events->mem, loaded->roots.source),
        .time = job->elapsed,
        .fetched = job->fetched,
      }});
  }

  spn_try(spn_session_apply_options(session, reresolve));

  if (*reresolve) {
    return SPN_OK;
  }

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SYNC_END,
    .sync_end = {
      .num_synced = si_da_size(packages),
      .time = elapsed,
    }});

  return SPN_OK;
}

spn_err_t sync_packages(spn_op_t* op, bool* reresolve) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = sync_jobs(op, scratch.mem, reresolve);
  sp_mem_end_scratch(scratch);
  return err;
}
