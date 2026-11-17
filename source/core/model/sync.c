#include "model/sync.h"

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
#include "git/patch.h"
#include "hash/digest/digest.h"
#include "lazy/lazy.h"
#include "lock/lock.h"
#include "model/resolve.h"
#include "op/op.h"
#include "paths/paths.h"
#include "pkg/load.h"
#include "pkg/types.h"
#include "project/types.h"
#include "resolve/types.h"
#include "semver/convert.h"
#include "session/session.h"
#include "session/types.h"
#include "spn/core.h"
#include "thread_pool/thread_pool.h"
#include "toolchain/probe.h"
#include "toolchain/provision.h"
#include "toolchain/toolchain.h"
#include "toolchain/types.h"
#include "external/http.h"
#include "unit/types.h"
#include "unit/unit.h"
#include "when/when.h"
#include "external/wasm/wasm.h"

typedef struct {
  spn_op_t* op;
  spn_session_t* session;
  spn_resolved_pkg_t* pkg;
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

static spn_wasi_spelling_t wasi_spelling(const spn_path_roots_t* roots, sp_mem_t mem, const spn_toolchain_info_t* toolchain) {
  sp_da_for(toolchain->rows, it) {
    if (toolchain->rows[it].triple.os == SPN_OS_WASI) {
      return spn_sdk_wasi_spelling(roots, mem, &toolchain->rows[it].sdk);
    }
  }
  return SPN_WASI_SPELLING_WASI;
}

static spn_err_t setup_artifact(spn_toolchain_store_t* store, spn_toolchain_unit_t* unit, sp_atomic_s32_t* cancelled) {
  spn_toolchain_info_t* toolchain = unit->info;
  spn_artifact_t artifact = toolchain->support.artifact;
  sp_tm_timer_t timer = sp_tm_start_timer();

  spn_toolchain_claim_t claim = spn_toolchain_claim(store, spn.mem, toolchain->name.str, artifact);
  spn_path_t root = claim.root;
  bool cached = claim.present;
  sp_str_t url = artifact.url;
  if (!cached) {
    spn_http_config_t http = {
      .ca_bundle = store->trust.bundle,
      .timeout_s = store->timeout_s,
      .cancelled = cancelled,
    };
    spn_http_failure_t failure = sp_zero;
    spn_err_t err = SPN_ERROR;
    sp_str_t actual = sp_zero;
    sp_path_t download = spn_path_at(store->roots, claim.tarball);

    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    sp_da(sp_str_t) urls = SP_NULLPTR;
    if (!sp_str_empty(store->mirror)) {
      urls = sp_da_new(scratch.mem, sp_str_t);
      sp_da_push(urls, spn_artifact_resolve_url(scratch.mem, artifact, store->mirror));
    }
    else {
      sp_str_t listing = sp_zero;
      if (!sp_str_empty(artifact.mirror_list)) {
        if (spn_http_download(&http, artifact.mirror_list, download, &failure) || sp_fs_read_at(scratch.mem, download, &listing)) {
          listing = artifact.mirror_snapshot;
        }
      }
      urls = spn_artifact_mirror_urls(scratch.mem, artifact, listing, sp_tm_now_point());
      sp_da_push(urls, artifact.url);
    }

    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_SYNC,
      .sync = {
        .name = toolchain->name.str,
        .url = sp_str_copy(spn.events->mem, urls[0]),
      }});
    sp_da_for(urls, it) {
      url = urls[it];
      spn_net_backoff_t backoff = it + 1 == sp_da_size(urls) ? store->backoff : spn_net_no_backoff;
      u32 retry = 0;
      do {
        err = spn_http_download(&http, url, download, &failure);
      } while (err && failure.kind == SPN_HTTP_DROPPED && spn_net_backoff_wait(backoff, retry++, url, sp_cstr_as_str(failure.message), cancelled));
      if (!err) {
        err = spn_toolchain_verify(store, &claim, spn.events->mem, &actual);
      }
      if (!err || failure.kind == SPN_HTTP_CANCELLED || failure.kind == SPN_HTTP_UNWRITABLE) {
        break;
      }
    }
    url = sp_str_copy(spn.events->mem, url);
    sp_mem_end_scratch(scratch);

    if (err == SPN_ERR_TOOLCHAIN_READ || err == SPN_ERR_TOOLCHAIN_SHA) {
      spn_toolchain_release(store, &claim);
      return spn_err_emit(&spn, (spn_err_union_t) {
        .kind = err,
        .artifact = {
          .name = toolchain->name.str,
          .url = url,
          .expected = artifact.sha256,
          .actual = actual,
        },
      });
    }
    if (err) {
      spn_toolchain_release(store, &claim);
      switch (failure.kind) {
        case SPN_HTTP_CANCELLED: {
          return SPN_ERR_CANCELLED;
        }
        case SPN_HTTP_UNWRITABLE: {
          return spn_err_emit(&spn, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = claim.tarball } });
        }
        case SPN_HTTP_UNTRUSTED: {
          return spn_err_emit(&spn, (spn_err_union_t) {
            .kind = SPN_ERR_CERT_UNTRUSTED,
            .subject = toolchain->name.str,
            .cert = {
              .url = url,
              .trust = store->trust,
              .config = spn_path_str(store->roots, spn.events->mem, spn.paths.config),
              .cause = sp_str_copy(spn.events->mem, sp_cstr_as_str(failure.message)),
            },
          });
        }
        case SPN_HTTP_DROPPED:
        case SPN_HTTP_FAILED: {
          return spn_err_emit(&spn, (spn_err_union_t) {
            .kind = SPN_ERR_TOOLCHAIN_FETCH,
            .artifact = {
              .name = toolchain->name.str,
              .url = url,
              .output = sp_str_copy(spn.events->mem, sp_cstr_as_str(failure.message)),
            },
          });
        }
      }
    }
    spn_try(spn_toolchain_install(store, &claim, toolchain->name.str, url));
  }

  spn_toolchain_launcher_t cxx = toolchain->cxx;
  if (!spn_arg_empty(toolchain->cxx.program)) {
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

spn_err_t spn_sync_toolchain(spn_toolchain_store_t* store, spn_toolchain_unit_t* unit, sp_atomic_s32_t* cancelled) {
  switch (unit->info->support.kind) {
    case SPN_TOOLCHAIN_SUPPORT_LOCAL: {
      spn_try(setup_local(store, unit));
      break;
    }
    case SPN_TOOLCHAIN_SUPPORT_ARTIFACT: {
      spn_try(setup_artifact(store, unit, cancelled));
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

  unit->cc.wasi = wasi_spelling(store->roots, spn.mem, unit->info);
  if (unit->info->driver == SPN_CC_DRIVER_ZIG) {
    sp_assert(unit->identity);
    sp_str_t id = sp_fmt(spn.mem, "{:0>16x}", sp_fmt_uint(unit->identity)).value;
    unit->cc.cache = spn_path_s(spn.mem, SPN_DIR_ID_TOOLCHAIN_EXTERNAL, sp_fmt(spn.mem, "zig/cache/{}", sp_fmt_str(id)).value);
    spn_try(spn_toolchain_generation(store->roots, unit->cc.cache, &unit->generation));
  }
  return SPN_OK;
}

static spn_err_t materialize_tree(spn_session_t* session, sp_intern_str_t name, spn_pkg_root_t tree, sp_atomic_s32_t* cancelled, spn_path_t* root, bool* fetched) {
  switch (tree.kind) {
    case SPN_PKG_ROOT_LOCAL: {
      spn_path_t canonical = spn_path_canonicalize(spn.mem, &session->ctx->roots, tree.local);
      sp_sys_file_meta_t meta = sp_zero;
      if (sp_fs_get_path_metadata_at(spn_path_at(&session->ctx->roots, canonical), &meta)) {
        return spn_err_emit(session->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_NO_MANIFEST,
          .no_manifest = { .path = spn_path_str(&session->ctx->roots, spn.events->mem, tree.local) },
        });
      }
      *root = canonical;
      return SPN_OK;
    }
    case SPN_PKG_ROOT_GIT: {
      bool fetching = false;
      spn_git_checkout_t* checkout = spn_git_cache_checkout(&session->ctx->caches.git, tree.git, &fetching);
      if (fetching) {
        spn_event_buffer_push(spn.events, (spn_event_t){
          .kind = SPN_EVENT_SYNC,
          .sync = {
            .name = name.str,
            .url = tree.git.url,
          }
        });
      }

      if (spn_git_checkout_ensure(&session->ctx->caches.git, checkout, cancelled)) {
        sp_str_t cause = sp_str_copy(spn.events->mem, sp_cstr_as_str(checkout->failure.message));
        switch (checkout->failure.kind) {
          case SPN_GIT_CANCELLED: {
            return SPN_ERR_CANCELLED;
          }
          case SPN_GIT_UNTRUSTED: {
            return spn_err_emit(session->ctx, (spn_err_union_t) {
              .kind = SPN_ERR_CERT_UNTRUSTED,
              .subject = name.str,
              .cert = {
                .url = tree.git.url,
                .trust = session->ctx->trust,
                .config = spn_path_str(&session->ctx->roots, spn.events->mem, session->ctx->paths.config),
                .cause = cause,
                .git = true,
              },
            });
          }
          case SPN_GIT_FAILED:
          case SPN_GIT_DROPPED:
          case SPN_GIT_UNAUTHORIZED:
          case SPN_GIT_CORRUPT: {
            if (checkout->failure.remote) {
              return spn_err_emit(session->ctx, (spn_err_union_t) {
                .kind = SPN_ERR_GIT_FETCH,
                .subject = name.str,
                .git_fetch = {
                  .url = tree.git.url,
                  .config = spn_path_str(&session->ctx->roots, spn.events->mem, session->ctx->paths.config),
                  .cause = cause,
                  .route = checkout->failure.route,
                },
              });
            }
            spn_event_buffer_push(spn.events, (spn_event_t) {
              .kind = SPN_EVENT_SYNC_FAILED,
              .sync_failed = {
                  .name = name.str,
                  .url = tree.git.url,
                  .error = sp_str_empty(cause) ? sp_str_lit("failed to fetch repository") : cause,
              }
            });
            return SPN_ERROR;
          }
        }
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
  bool* fetched = &job->fetched;

  if (job->patched) {
    bool fetching = false;
    spn_git_cache_checkout(&session->ctx->caches.git, pkg->origin.source.git, &fetching);
    if (fetching) {
      spn_event_buffer_push(spn.events, (spn_event_t) {
        .kind = SPN_EVENT_SYNC_PATCH,
        .sync = {
          .name = pkg->name.str,
          .url = pkg->origin.source.git.url,
        }});
    }
  }

  spn_try(materialize_tree(session, pkg->name, pkg->origin.recipe, &job->op->cancelled, &pkg->roots.recipe, fetched));

  if (pkg->origin.source.kind == SPN_PKG_ROOT_NONE) {
    pkg->roots.source = pkg->roots.recipe;
  } else {
    spn_try(materialize_tree(session, pkg->name, pkg->origin.source, &job->op->cancelled, &pkg->roots.source, fetched));
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
  job->err = spn_sync_toolchain(&spn.caches.toolchains, job->unit, &job->op->cancelled);
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
      if (patch->name.id != pkg->id.name) {
        continue;
      }
      if (pkg->origin.source.kind != SPN_PKG_ROOT_GIT) {
        spn_err_t kind = spn_err_emit(session->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_PATCH_NOT_GIT,
          .subject = patch->name.str,
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

  si_da_for_value(session->builds.toolchains, unit) {
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
    spn_thread_pool_submit(&pool, (spn_thread_pool_job_t) { .fn = sync_package_node, .data = job });
  }
  si_da_for_value(toolchains, job) {
    spn_thread_pool_submit(&pool, (spn_thread_pool_job_t) { .fn = sync_toolchain_node, .data = job });
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

    spn_path_t path = spn_path_join(spn.mem, pkg->roots.recipe, pkg->origin.paths.manifest);

    pkg->manifest = pkg->origin.manifest;
    if (!pkg->manifest) {
      sp_sys_file_meta_t meta = sp_zero;
      if (sp_fs_get_path_metadata_at(spn_path_at(roots, path), &meta) || meta.kind == SP_FS_KIND_UNKNOWN) {
        return spn_err_emit(session->ctx, (spn_err_union_t) {
          .kind = SPN_ERR_NO_MANIFEST,
          .subject = pkg->name.str,
          .no_manifest = { .path = spn_path_str(roots, spn.events->mem, path) },
        });
      }
      si_pkg_info_t* parsed = sp_alloc_type(session->mem, si_pkg_info_t);
      spn_err_union_t err = sp_zero;
      if (spn_pkg_load(session->mem, session->ctx->intern, roots, path, parsed, &err)) {
        return spn_err_emit(session->ctx, spn_err_about(err, pkg->name.str));
      }
      pkg->manifest = parsed;
    }

    if (pkg->manifest->name.id != pkg->name.id) {
      return spn_err_emit(session->ctx, (spn_err_union_t) {
        .kind = SPN_ERR_PKG_MISMATCH,
        .mismatch = { .path = spn_path_str(roots, spn.events->mem, path), .declared = pkg->manifest->name.str, .requested = pkg->name.str },
      });
    }

    pkg->options.decls = pkg->manifest->options;

    sp_str_t url = sp_str_lit("");
    if (pkg->origin.recipe.kind == SPN_PKG_ROOT_GIT) {
      url = pkg->origin.recipe.git.url;
    }
    if (pkg->origin.source.kind == SPN_PKG_ROOT_GIT) {
      url = pkg->origin.source.git.url;
    }
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_SYNC_PACKAGE, .pkg = pkg->manifest->name.str,
      .sync_pkg = {
        .name = pkg->name.str,
        .url = url,
        .source_path = spn_path_str(roots, spn.events->mem, pkg->roots.source),
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

static void add_name(sp_da(sp_str_t)* names, sp_str_t name) {
  sp_da_for(*names, it) {
    if (sp_str_equal((*names)[it], name)) {
      return;
    }
  }
  sp_da_push(*names, name);
}

static spn_lock_root_t pinned_root(spn_pkg_root_t root) {
  if (root.kind != SPN_PKG_ROOT_GIT) {
    return sp_zero_s(spn_lock_root_t);
  }
  return (spn_lock_root_t) { .url = root.git.url, .rev = root.git.rev, .dir = root.git.dir };
}

static spn_err_t pin(sp_mem_t scratch, spn_ctx_t* ctx, spn_resolve_t resolve, bool locked, const spn_lock_t* held, spn_lock_t* lock) {
  spn_lock_t selected = spn_lock_new(scratch);
  sp_ht_for_kv(resolve, it) {
    const spn_resolved_pkg_t* pkg = it.val;
    if (pkg->source != SPN_PKG_SOURCE_INDEX) {
      continue;
    }
    spn_lock_insert(&selected, (spn_lock_entry_t) {
      .name = pkg->name,
      .version = pkg->id.version,
      .source = pinned_root(pkg->origin.release->source),
      .manifest = pinned_root(pkg->origin.release->manifest),
    });
  }

  sp_da(spn_err_union_t) moved = sp_da_new(scratch, spn_err_union_t);
  spn_lock_diff_t drift = spn_lock_diff(lock, &selected);
  while (spn_lock_diff_next(&drift)) {
    if (drift.was && drift.now) {
      sp_da_push(moved, spn_lock_mismatch(drift.was, drift.now));
    }
  }
  if (!sp_da_empty(moved)) {
    sp_da(sp_str_t) accept = sp_da_new(ctx->events->mem, sp_str_t);
    spn_lock_diff_t unpinned = spn_lock_diff(held, lock);
    while (spn_lock_diff_next(&unpinned)) {
      if (unpinned.was) {
        add_name(&accept, unpinned.was->name.str);
      }
    }
    sp_da_for(moved, it) {
      add_name(&accept, moved[it].subject);
    }
    sp_da_for(moved, it) {
      moved[it].lock_mismatch.update = accept;
      spn_err_emit(ctx, moved[it]);
    }
    return SPN_ERR_LOCK_MISMATCH;
  }

  sp_da_for(selected.entries, it) {
    spn_lock_insert(lock, selected.entries[it]);
  }

  if (locked && held->version != lock->version) {
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_LOCK_STALE });
  }
  spn_lock_diff_t stale = spn_lock_diff(held, lock);
  if (locked && spn_lock_diff_next(&stale)) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_LOCK_STALE,
      .subject = (stale.was ? stale.was : stale.now)->name.str,
    });
  }
  return SPN_OK;
}

spn_err_t spn_resolve_and_sync(spn_op_t* op, const spn_lock_t* held, const spn_lock_t* pins) {
  spn_session_t* session = op->session;
  spn_ctx_t* ctx = session->ctx;
  const spn_path_roots_t* roots = &ctx->roots;

  spn_err_t hashed = SPN_OK;
  si_da_for_ptr(session->project->patches, decl) {
    spn_patch_t patch = { .name = decl->name };
    si_da_for_value(decl->files, file) {
      si_da_push(session->mem, patch.set.files, spn_path_str(roots, session->mem, file));
    }
    u32 missing = 0;
    patch.set.hash = spn_git_patch_hash(patch.set.files, &missing);
    if (!patch.set.hash) {
      spn_err_t kind = spn_err_emit(ctx, (spn_err_union_t) {
        .kind = SPN_ERR_PATCH_FILE_MISSING,
        .subject = decl->name.str,
        .patch_file = { .path = patch.set.files[missing] },
      });
      if (!hashed) {
        hashed = kind;
      }
      continue;
    }
    si_da_push(session->mem, session->patches, patch);
  }
  spn_try(hashed);

  spn_lock_t lock = spn_lock_new(session->mem);
  sp_da_for(pins->entries, it) {
    spn_lock_insert(&lock, pins->entries[it]);
  }

  bool reresolve = sp_zero;
  do {
    spn_try(spn_resolve(op, &lock));

    sp_mem_arena_marker_t pinning = sp_mem_begin_scratch();
    spn_err_t pinned = pin(pinning.mem, ctx, session->resolve, session->locked, held, &lock);
    sp_mem_end_scratch(pinning);
    spn_try(pinned);

    sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
    spn_err_t synced = sync_jobs(op, scratch.mem, &reresolve);
    sp_mem_end_scratch(scratch);
    spn_try(synced);
  } while (reresolve);

  bool changed = held->version != lock.version;
  bool unlocked = false;
  spn_lock_diff_t diff = spn_lock_diff(held, &lock);
  while (spn_lock_diff_next(&diff)) {
    changed = true;
    unlocked = unlocked || diff.was;
  }
  diff = spn_lock_diff(held, &lock);
  while (unlocked && spn_lock_diff_next(&diff)) {
    const spn_lock_entry_t* entry = diff.now ? diff.now : diff.was;
    spn_event_buffer_push(ctx->events, (spn_event_t) {
      .kind = diff.now ? SPN_EVENT_LOCK_ADDED : SPN_EVENT_LOCK_REMOVED,
      .pkg = entry->name.str,
      .resolve_pkg = {
        .name = entry->name.str,
        .version = spn_semver_to_str(ctx->events->mem, entry->version),
      },
    });
  }
  spn_err_union_t unwritten = sp_zero;
  if (changed && spn_lock_save(roots, session->project->paths.lock, &lock, &unwritten)) {
    return spn_err_emit(ctx, unwritten);
  }

  spn_err_t unused = SPN_OK;
  si_da_for_ptr(session->patches, patch) {
    bool used = false;
    sp_ht_for_kv(session->resolve, it) {
      if (it.key->name == patch->name.id) {
        used = true;
        break;
      }
    }
    if (used) {
      continue;
    }
    spn_err_t kind = spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_PATCH_UNUSED,
      .subject = patch->name.str,
    });
    if (!unused) {
      unused = kind;
    }
  }
  return unused;
}
