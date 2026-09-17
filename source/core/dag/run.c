#include "dag/dag.h"
#include "dag/stamp.h"
#include "dag/types.h"
#include "core/core.h"
#include "paths/paths.h"
#include "thread_pool/thread_pool.h"
#include "hash/digest/digest.h"
#include "sp.h"
#include "spn/core.h"
#include "fs/fs.h"
#include "sp/sp_glob.h"


static bool is_timespec_equal(sp_sys_timespec_t a, sp_sys_timespec_t b) {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
}

static sp_str_t parent_dir(sp_str_t path) {
  s32 index = sp_str_find_c8_reverse(path, '/');
  return index == SP_STR_NO_MATCH ? sp_str_lit("") : sp_str_prefix(path, index);
}

static spn_dag_file_meta_t file_meta_from_sys(sp_sys_file_meta_t sys) {
  return (spn_dag_file_meta_t) {
    .id = {
      .device = sys.device,
      .inode = sys.id
    },
    .mtime = sys.mtime,
    .size = sys.size,
  };
}

static bool file_meta_current(spn_dag_file_meta_t meta, sp_sys_file_meta_t sys) {
  if (meta.id.device && meta.id.device != sys.device) return false;
  if (meta.id.inode != sys.id) return false;
  if (!is_timespec_equal(meta.mtime, sys.mtime)) return false;
  return meta.size == sys.size;
}

void spn_dag_file_cache_init(spn_dag_file_cache_t* c, sp_mem_t mem, const spn_path_roots_t* roots) {
  c->arena = sp_mem_arena_new(mem);
  c->mem = sp_mem_arena_as_allocator(c->arena);
  c->roots = roots;
  sp_ht_init(c->mem, c->entries);
  sp_ht_init(c->mem, c->metadata);
  sp_ht_set_fns(c->metadata, spn_path_on_hash, spn_path_on_compare);
  sp_ht_init(c->mem, c->hints);
  sp_ht_set_fns(c->hints, spn_path_on_hash, spn_path_on_compare);
  sp_str_ht_init(c->mem, c->canonical);
}

sp_str_t spn_dag_file_cache_canonical(spn_dag_file_cache_t* c, sp_str_t path) {
  sp_mutex_lock(&c->mutex);
  sp_str_t* cached = sp_ht_getp(c->canonical, path);
  if (cached) {
    sp_str_t result = *cached;
    sp_mutex_unlock(&c->mutex);
    return result;
  }
  sp_mutex_unlock(&c->mutex);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_str_t canonical = sp_fs_canonicalize_path(s.mem, path);

  sp_mutex_lock(&c->mutex);
  canonical = sp_str_copy(c->mem, canonical);
  sp_ht_insert(c->canonical, sp_str_copy(c->mem, path), canonical);
  sp_mutex_unlock(&c->mutex);
  sp_mem_end_scratch(s);
  return canonical;
}

void spn_dag_file_cache_fence(spn_dag_file_cache_t* c, sp_sys_timespec_t fence) {
  sp_mutex_lock(&c->mutex);
  c->stamp = (spn_dag_stamp_t) { .fence = fence };
  sp_mutex_unlock(&c->mutex);
}

spn_err_t spn_dag_file_cache_fence_dir(spn_dag_file_cache_t* c, sp_str_t dir) {
  sp_sys_timespec_t fence = sp_zero;
  spn_try(spn_dag_stamp_probe(dir, &fence));

  sp_mutex_lock(&c->mutex);
  c->stamp = (spn_dag_stamp_t) {
    .fence = fence,
    .dir = sp_str_copy(c->mem, dir)
  };
  sp_mutex_unlock(&c->mutex);
  return SPN_OK;
}

void spn_dag_file_cache_invalidate(spn_dag_file_cache_t* c, spn_path_t path) {
  sp_mutex_lock(&c->mutex);
  sp_ht_erase(c->metadata, path);
  sp_mutex_unlock(&c->mutex);
}

void spn_dag_file_cache_invalidate_all(spn_dag_file_cache_t* c) {
  sp_mutex_lock(&c->mutex);
  sp_ht_clear(c->metadata);
  sp_mutex_unlock(&c->mutex);
}

spn_err_t spn_dag_file_cache_stat(spn_dag_file_cache_t* c, spn_path_t path, sp_sys_file_meta_t* meta) {
  sp_mutex_lock(&c->mutex);
  sp_sys_file_meta_t* cached = sp_ht_getp(c->metadata, path);
  if (cached) {
    *meta = *cached;
    sp_mutex_unlock(&c->mutex);
    return SPN_OK;
  }
  sp_mutex_unlock(&c->mutex);

  if (c->stats) {
    sp_atomic_u32_add(&c->stats->stats, 1, SP_ATOMIC_RELAXED);
  }
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_sys_file_meta_t sys = sp_zero;
  sp_err_t rc = sp_sys_get_path_metadata_s(sp_sys_get_root(0), spn_path_str(c->roots, s.mem, path), &sys);
  sp_mem_end_scratch(s);
  if (rc) {
    return SPN_ERR_DAG_STAT;
  }

  sp_mutex_lock(&c->mutex);
  sp_ht_insert(c->metadata, spn_path_copy(c->mem, path), sys);
  sp_mutex_unlock(&c->mutex);
  *meta = sys;
  return SPN_OK;
}

bool spn_dag_file_cache_recorded(spn_dag_file_cache_t* c, spn_path_t path) {
  sp_sys_file_meta_t sys = sp_zero;
  if (spn_dag_file_cache_stat(c, path, &sys)) {
    return false;
  }
  sp_mutex_lock(&c->mutex);
  spn_dag_file_meta_t* hint = sp_ht_getp(c->hints, path);
  bool recorded = hint && file_meta_current(*hint, sys);
  sp_mutex_unlock(&c->mutex);
  return recorded;
}

static void file_cache_record(spn_dag_file_cache_t* c, spn_path_t path, spn_dag_file_meta_t meta) {
  sp_ht_insert(c->entries, meta.id, meta);
  sp_ht_insert(c->hints, spn_path_copy(c->mem, path), meta);
  c->hints_dirty = true;
}

spn_err_t spn_dag_file_cache_digest(spn_dag_file_cache_t* c, spn_path_t path, spn_dag_digest_t* digest) {
  sp_sys_file_meta_t sys = sp_zero;
  spn_try(spn_dag_file_cache_stat(c, path, &sys));

  spn_dag_file_meta_t fresh = file_meta_from_sys(sys);
  sp_mutex_lock(&c->mutex);
  spn_dag_file_meta_t* cached = sp_ht_getp(c->entries, fresh.id);
  if (cached && file_meta_current(*cached, sys)) {
    *digest = cached->digest;
    sp_mutex_unlock(&c->mutex);
    return SPN_OK;
  }

  spn_dag_file_meta_t* hint = sp_ht_getp(c->hints, path);
  if (hint && file_meta_current(*hint, sys) && spn_dag_digest_valid(hint->digest)) {
    hint->id.device = sys.device;
    *digest = hint->digest;
    sp_ht_insert(c->entries, hint->id, *hint);
    sp_mutex_unlock(&c->mutex);
    return SPN_OK;
  }

  bool record = false;
  spn_err_t admitted = spn_dag_stamp_admit(&c->stamp, fresh.mtime, &record);
  sp_mutex_unlock(&c->mutex);
  spn_try(admitted);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  u64 size = 0;
  spn_err_t err = spn_digest_file(SPN_DIGEST_BLAKE3, spn_path_str(c->roots, s.mem, path), digest->bytes, &size);
  sp_mem_end_scratch(s);
  spn_try(err);
  if (c->stats) {
    sp_atomic_u32_add(&c->stats->hashed_files, 1, SP_ATOMIC_RELAXED);
    sp_atomic_u64_add(&c->stats->hashed_bytes, size, SP_ATOMIC_RELAXED);
  }

  if (record) {
    fresh.digest = *digest;
    sp_mutex_lock(&c->mutex);
    file_cache_record(c, path, fresh);
    sp_mutex_unlock(&c->mutex);
  }
  return SPN_OK;
}

spn_err_t spn_dag_file_cache_seed(spn_dag_file_cache_t* c, spn_path_t path, spn_dag_digest_t digest) {
  spn_dag_file_cache_invalidate(c, path);
  sp_sys_file_meta_t sys = sp_zero;
  spn_try(spn_dag_file_cache_stat(c, path, &sys));

  spn_dag_file_meta_t meta = file_meta_from_sys(sys);
  meta.digest = digest;

  sp_mutex_lock(&c->mutex);
  bool record = false;
  spn_err_t admitted = spn_dag_stamp_admit(&c->stamp, meta.mtime, &record);
  if (!admitted && record) {
    file_cache_record(c, path, meta);
  }
  sp_mutex_unlock(&c->mutex);
  return admitted;
}

static void diag_set(spn_dag_diag_t* diag, spn_err_t err, spn_dag_id_t action, sp_str_t path) {
  if (!diag || diag->err) {
    return;
  }
  diag->err = err;
  diag->action = action;
  diag->path = path;
}

static void trace_emit(spn_dag_env_t* env, spn_dag_trace_event_t event) {
  if (env->trace) {
    env->trace(&event, env->trace_data);
  }
}

static void trace_resolve(spn_dag_env_t* env, spn_dag_id_t action, bool hit) {
  trace_emit(env, (spn_dag_trace_event_t) {
    .kind = SPN_DAG_TRACE_RESOLVE,
    .action = action,
    .hit = hit
  });
}

static void progress_total(spn_dag_env_t* env, u64 total) {
  if (env->progress) {
    sp_atomic_s32_store(&env->progress->total, (s32)total, SP_ATOMIC_SEQ_CST);
    if (env->wake) {
      spn_wake_ring(env->wake);
    }
  }
}

static void progress_count(spn_dag_env_t* env, const spn_dag_action_t* action, bool hit) {
  if (!env->progress) {
    return;
  }
  switch (action->kind) {
    case SPN_DAG_ACTION_STATIC:
    case SPN_DAG_ACTION_DISCOVERED: {
      sp_atomic_s32_add(hit ? &env->progress->hits : &env->progress->misses, 1, SP_ATOMIC_SEQ_CST);
      break;
    }
    case SPN_DAG_ACTION_UNCACHEABLE: {
      sp_assert(!hit);
      break;
    }
  }
  sp_atomic_s32_add(&env->progress->completed, 1, SP_ATOMIC_SEQ_CST);
  if (env->wake) {
    spn_wake_ring(env->wake);
  }
}

static sp_str_t artifact_render(spn_dag_t* g, spn_path_t path) {
  sp_mutex_lock(&g->mutex);
  sp_str_t rendered = spn_path_str(g->roots, g->mem, path);
  sp_mutex_unlock(&g->mutex);
  return rendered;
}

static void diag_set_path(spn_dag_diag_t* diag, spn_err_t err, spn_dag_id_t action, spn_dag_t* g, spn_path_t path) {
  if (!diag || diag->err) {
    return;
  }
  diag_set(diag, err, action, artifact_render(g, path));
}

static sp_str_t artifact_location(spn_dag_t* g, spn_dag_artifact_t* artifact) {
  return spn_path_empty(artifact->path) ? artifact->name : artifact_render(g, artifact->path);
}

typedef enum {
  TARGET_SETTLED,
  TARGET_STALE,
  TARGET_POISONED,
} target_state_t;

static target_state_t target_state(spn_dag_env_t* env, spn_path_t path, sp_str_t name, spn_dag_digest_t digest) {
  sp_sys_file_meta_t sys = sp_zero;
  spn_dag_digest_t existing = sp_zero;
  if (spn_dag_file_cache_stat(env->files, path, &sys) || spn_dag_file_cache_digest(env->files, path, &existing)) {
    return TARGET_STALE;
  }
  if (spn_dag_digest_equal(existing, digest)) {
    return TARGET_SETTLED;
  }
  return spn_dag_store_owns(env->store, digest, name, sys) ? TARGET_POISONED : TARGET_STALE;
}

static spn_err_t link_target(spn_dag_env_t* env, spn_path_t path, sp_str_t name, spn_dag_digest_t digest) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = spn_dag_store_materialize(env->store, digest, name, spn_path_str(env->files->roots, s.mem, path));
  sp_mem_end_scratch(s);
  spn_try(err);
  return spn_dag_file_cache_seed(env->files, path, digest);
}

static spn_err_t settle_tree(spn_dag_t* g, spn_dag_action_t* action, spn_dag_artifact_t* artifact, spn_dag_env_t* env, spn_dag_diag_t* diag) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = SPN_OK;

  sp_da(spn_dag_action_output_t) entries = sp_zero;
  err = spn_dag_tree_entries(env->store, artifact->digest, s.mem, &entries);
  if (err) {
    diag_set_path(diag, err, action->id, g, artifact->path);
    goto done;
  }
  spn_path_t* paths = sp_alloc_n(s.mem, spn_path_t, sp_da_size(entries) ? sp_da_size(entries) : 1);
  sp_da_for(entries, it) {
    paths[it] = spn_path_join(s.mem, artifact->path, entries[it].name);
  }

  sp_str_t dir = spn_path_str(g->roots, s.mem, artifact->path);
  bool exists = sp_fs_is_dir(dir);
  sp_da(sp_fs_entry_t) present = sp_zero;
  sp_da(u32) stale = sp_da_new(s.mem, u32);
  sp_da(u32) poisoned = sp_da_new(s.mem, u32);
  u64 files = 0;
  if (exists) {
    if (sp_fs_collect_recursive(s.mem, dir, &present)) {
      err = SPN_ERR_DAG_STORE_READ;
      diag_set(diag, err, action->id, dir);
      goto done;
    }
    sp_da_for(present, it) {
      files += present[it].kind != SP_FS_KIND_DIR;
    }
    sp_da_for(entries, it) {
      switch (target_state(env, paths[it], entries[it].name, entries[it].digest)) {
        case TARGET_SETTLED:  break;
        case TARGET_STALE:    sp_da_push(stale, (u32)it); break;
        case TARGET_POISONED: sp_da_push(poisoned, (u32)it); break;
      }
    }
  }
  else {
    if (sp_fs_create_dir(dir)) {
      err = SPN_ERR_DAG_STORE_WRITE;
      diag_set(diag, err, action->id, dir);
      goto done;
    }
    sp_da_for(entries, it) {
      sp_da_push(stale, (u32)it);
    }
  }

  if (exists && sp_da_empty(stale) && sp_da_empty(poisoned) && files == sp_da_size(entries)) {
    trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = artifact->digest, .hit = true });
    goto done;
  }
  action->wrote = true;

  sp_da_for(poisoned, it) {
    spn_dag_store_drop(env->store, entries[poisoned[it]].digest, entries[poisoned[it]].name);
  }
  if (!sp_da_empty(poisoned)) {
    err = SPN_ERR_DAG_STORE_MISSING;
    diag_set_path(diag, err, action->id, g, paths[poisoned[0]]);
    goto done;
  }

  sp_str_ht(bool) keep_files = SP_NULLPTR;
  sp_str_ht(bool) keep_dirs = SP_NULLPTR;
  sp_str_ht_init(s.mem, keep_files);
  sp_str_ht_init(s.mem, keep_dirs);
  sp_da_for(entries, it) {
    sp_str_ht_insert(keep_files, entries[it].name, true);
    for (sp_str_t parent = sp_fs_parent_path(entries[it].name); !sp_str_empty(parent); parent = sp_fs_parent_path(parent)) {
      sp_str_ht_insert(keep_dirs, parent, true);
    }
  }
  sp_da_rfor(present, it) {
    sp_fs_entry_t* entry = &present[it];
    sp_str_t name = sp_str_strip_left(sp_str_strip_left(entry->path, dir), sp_str_lit("/"));
    sp_err_t removed = SP_OK;
    switch (entry->kind) {
      case SP_FS_KIND_DIR: {
        if (sp_str_ht_get(keep_dirs, name)) {
          continue;
        }
        removed = sp_fs_remove_dir(entry->path);
        break;
      }
      case SP_FS_KIND_FILE:
      case SP_FS_KIND_SYMLINK:
      case SP_FS_KIND_NONE: {
        if (sp_str_ht_get(keep_files, name)) {
          continue;
        }
        removed = sp_fs_remove_file(entry->path);
        break;
      }
    }
    if (removed) {
      err = SPN_ERR_DAG_STORE_WRITE;
      diag_set(diag, err, action->id, entry->path);
      goto done;
    }
    spn_dag_file_cache_invalidate(env->files, spn_path_join(s.mem, artifact->path, name));
  }

  sp_da_for(stale, it) {
    spn_dag_action_output_t* entry = &entries[stale[it]];
    err = link_target(env, paths[stale[it]], entry->name, entry->digest);
    if (err) {
      diag_set_path(diag, err, action->id, g, paths[stale[it]]);
      goto done;
    }
  }
  trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = artifact->digest, .hit = false });

done:
  sp_mem_end_scratch(s);
  return err;
}

static spn_err_t settle_file(spn_dag_t* g, spn_dag_action_t* action, spn_dag_artifact_t* artifact, spn_dag_env_t* env, spn_dag_diag_t* diag) {
  switch (target_state(env, artifact->path, artifact->name, artifact->digest)) {
    case TARGET_SETTLED: {
      trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = artifact->digest, .hit = true });
      return SPN_OK;
    }
    case TARGET_POISONED: {
      spn_dag_store_drop(env->store, artifact->digest, artifact->name);
      diag_set_path(diag, SPN_ERR_DAG_STORE_MISSING, action->id, g, artifact->path);
      return SPN_ERR_DAG_STORE_MISSING;
    }
    case TARGET_STALE: {
      break;
    }
  }
  action->wrote = true;
  spn_err_t err = link_target(env, artifact->path, artifact->name, artifact->digest);
  if (err) {
    diag_set_path(diag, err, action->id, g, artifact->path);
    return err;
  }
  trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = artifact->digest, .hit = false });
  return SPN_OK;
}

static spn_err_t settle(spn_dag_t* g, spn_dag_action_t* action, spn_dag_env_t* env, spn_dag_diag_t* diag) {
  action->wrote = false;
  sp_da_for(action->produces, it) {
    spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    if (spn_path_empty(artifact->path)) {
      sp_mutex_lock(&g->mutex);
      spn_err_t located = spn_dag_store_locate(env->store, g->mem, artifact->digest, artifact->name, &artifact->materialized);
      sp_mutex_unlock(&g->mutex);
      if (located) {
        diag_set(diag, located, action->id, artifact->name);
        return located;
      }
      continue;
    }
    spn_try(artifact->kind == SPN_DAG_ARTIFACT_KIND_TREE
      ? settle_tree(g, action, artifact, env, diag)
      : settle_file(g, action, artifact, env, diag));
    artifact->materialized = artifact->path;
  }
  return SPN_OK;
}

static bool restore_entry(spn_dag_t* g, spn_dag_action_t* action, const spn_dag_action_entry_t* entry, spn_dag_env_t* env) {
  if (sp_da_size(entry->outputs) != sp_da_size(action->produces)) {
    return false;
  }

  sp_da_for(entry->outputs, it) {
    if (!sp_str_equal(entry->outputs[it].name, spn_dag_find_artifact(g, action->produces[it])->name)) {
      return false;
    }
  }

  sp_da_for(entry->outputs, it) {
    spn_dag_find_artifact(g, action->produces[it])->digest = entry->outputs[it].digest;
  }

  return !settle(g, action, env, SP_NULLPTR);
}

static bool try_restore(spn_dag_t* g, spn_dag_action_t* action, spn_dag_digest_t key, spn_dag_env_t* env) {
  spn_dag_action_entry_t entry = sp_zero;
  bool present = spn_dag_action_cache_get(env->cache, key, &entry);
  bool hit = present && restore_entry(g, action, &entry, env);
  trace_emit(env, (spn_dag_trace_event_t) {
    .kind = SPN_DAG_TRACE_CACHE,
    .action = action->id,
    .key = key,
    .present = present,
    .hit = hit,
  });
  if (present && !hit) {
    spn_dag_action_cache_remove(env->cache, key);
  }
  return hit;
}

static s32 obs_order(const void* a, const void* b) {
  const spn_dag_obs_t* oa = (const spn_dag_obs_t*)a;
  const spn_dag_obs_t* ob = (const spn_dag_obs_t*)b;
  if (oa->path.root != ob->path.root) {
    return (s32)oa->path.root - (s32)ob->path.root;
  }
  s32 order = sp_str_compare_alphabetical(oa->path.sub, ob->path.sub);
  if (order) {
    return order;
  }
  if (oa->kind != ob->kind) {
    return (s32)oa->kind - (s32)ob->kind;
  }
  return sp_str_compare_alphabetical(oa->filter, ob->filter);
}

static bool obs_equal(const spn_dag_obs_t* a, const spn_dag_obs_t* b) {
  return a->kind == b->kind && spn_path_equal(a->path, b->path) && sp_str_equal(a->filter, b->filter);
}

void spn_dag_obs_canonicalize(sp_da(spn_dag_obs_t) obs) {
  if (sp_da_empty(obs)) {
    return;
  }
  sp_da_sort(obs, obs_order);
  u64 w = 1;
  for (u64 r = 1; r < sp_da_size(obs); r++) {
    if (!obs_equal(&obs[r], &obs[w - 1])) {
      obs[w++] = obs[r];
    }
  }
  sp_da_head(obs)->size = w;
}

static s32 member_order(const void* a, const void* b) {
  return sp_str_compare_alphabetical(((const sp_fs_entry_t*)a)->name, ((const sp_fs_entry_t*)b)->name);
}

static spn_err_t membership_digest(sp_str_t dir, sp_str_t filter, spn_dag_digest_t* digest) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = SPN_OK;

  sp_glob_t* glob = SP_NULLPTR;
  if (!sp_str_empty(filter)) {
    glob = sp_glob_new_str(s.mem, filter);
    if (!glob) {
      err = SPN_ERR_DAG_GLOB;
      goto done;
    }
  }

  sp_da(sp_fs_entry_t) members = sp_da_new(s.mem, sp_fs_entry_t);
  sp_da(sp_fs_entry_t) entries = sp_zero;
  sp_fs_collect(s.mem, dir, &entries);
  sp_da_for(entries, it) {
    if (entries[it].kind != SP_FS_KIND_DIR && glob && !sp_glob_match(glob, entries[it].name)) {
      continue;
    }
    sp_da_push(members, entries[it]);
  }
  sp_da_sort(members, member_order);

  spn_digest_ctx_t ctx = sp_zero;
  spn_digest_init_blake3(&ctx);
  spn_dag_hash_str(&ctx, sp_str_lit("spn.dag.enum.v1"));
  spn_dag_hash_u64(&ctx, sp_da_size(members));
  sp_da_for(members, it) {
    spn_dag_hash_str(&ctx, members[it].name);
    spn_dag_hash_u8(&ctx, (u8)members[it].kind);
  }
  *digest = spn_dag_hash_final(&ctx);

done:
  sp_mem_end_scratch(s);
  return err;
}

static spn_err_t resolve_one(spn_dag_file_cache_t* files, const spn_dag_obs_t* o, spn_dag_digest_t* digest, sp_mem_t mem) {
  *digest = (spn_dag_digest_t) sp_zero;
  switch (o->kind) {
    case SPN_DAG_OBS_ENUMERATION: {
      return membership_digest(spn_path_str(files->roots, mem, o->path), o->filter, digest);
    }
    case SPN_DAG_OBS_ABSENT: {
      sp_sys_file_meta_t sys = sp_zero;
      sp_err_t rc = sp_sys_get_path_metadata_s(sp_sys_get_root(0), spn_path_str(files->roots, mem, o->path), &sys);
      if (rc == SP_ERR_SYS_NOT_FOUND) {
        return SPN_OK;
      }
      if (rc) {
        return SPN_ERR_DAG_STAT;
      }
      break;
    }
    case SPN_DAG_OBS_FILE: {
      break;
    }
  }

  sp_sys_file_meta_t sys = sp_zero;
  spn_try(spn_dag_file_cache_stat(files, o->path, &sys));

  if (sys.kind == SP_FS_KIND_DIR) {
    return membership_digest(spn_path_str(files->roots, mem, o->path), sp_str_lit(""), digest);
  }

  return spn_dag_file_cache_digest(files, o->path, digest);
}

static spn_err_t resolve_observations(spn_dag_file_cache_t* files, const spn_dag_obs_t* obs, u32 count, spn_dag_digest_t* digests) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = SPN_OK;
  sp_for(it, count) {
    err = resolve_one(files, &obs[it], &digests[it], s.mem);
    if (err) {
      break;
    }
  }
  sp_mem_end_scratch(s);
  return err;
}

static void record(spn_dag_t* g, spn_dag_action_t* action, spn_dag_digest_t key, spn_dag_env_t* env) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();

  sp_da(spn_dag_action_output_t) outputs = sp_da_new(s.mem, spn_dag_action_output_t);
  sp_da_for(action->produces, it) {
    spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    sp_da_push(outputs, ((spn_dag_action_output_t) {
      .name = artifact->name,
      .digest = artifact->digest
    }));
  }
  spn_dag_action_cache_put(env->cache, key, outputs, (u32)sp_da_size(outputs));

  sp_mem_end_scratch(s);
}

typedef struct {
  spn_dag_action_t* action;
  spn_dag_digest_t key;
  bool hit;
  spn_dag_digest_t* digests;
  spn_dag_obs_set_t obs;
  spn_dag_diag_t diag;
} spn_dag_attempt_t;

static spn_path_t scratch_dir(sp_mem_t mem, spn_dag_env_t* env, spn_dag_action_t* action) {
  return spn_path_join(mem, env->scratch, sp_fmt(mem, "scratch/{}", sp_fmt_uint(action->id.index)).value);
}

static void attempt_discard(spn_dag_t* g, spn_dag_env_t* env, spn_dag_attempt_t* attempt) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_fs_remove_dir(spn_path_str(g->roots, s.mem, scratch_dir(s.mem, env, attempt->action)));
  sp_mem_end_scratch(s);
}

static void diag_flush(spn_dag_env_t* env, spn_dag_attempt_t* attempt, spn_err_t err) {
  if (!err) {
    return;
  }
  if (attempt->diag.err) {
    diag_set(&env->diag, attempt->diag.err, attempt->diag.action, attempt->diag.path);
  }
  else {
    spn_dag_id_t action = attempt->action ? attempt->action->id : (spn_dag_id_t) sp_zero;
    diag_set(&env->diag, err, action, sp_str_lit(""));
  }
}

static spn_dag_digest_t weak_key_traced(spn_dag_t* g, spn_dag_action_t* action, spn_dag_env_t* env) {
  spn_dag_digest_t key = spn_dag_weak_key(g, action->id);
  trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_KEY, .action = action->id, .key = key });
  return key;
}

static void lookup(spn_dag_t* g, spn_dag_action_t* action, spn_dag_env_t* env, spn_dag_attempt_t* attempt) {
  attempt->action = action;

  switch (action->kind) {
    case SPN_DAG_ACTION_STATIC: {
      attempt->key = weak_key_traced(g, action, env);
      attempt->hit = try_restore(g, action, attempt->key, env);
      break;
    }
    case SPN_DAG_ACTION_DISCOVERED: {
      attempt->key = weak_key_traced(g, action, env);
      spn_dag_pathset_t set = sp_zero;
      bool present = spn_dag_obs_table_get(env->discovery, attempt->key, &set);
      trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_DISCOVERY, .action = action->id, .key = attempt->key, .hit = present });
      if (!present) {
        break;
      }
      sp_mem_arena_marker_t s = sp_mem_begin_scratch();
      u32 count = (u32)sp_da_size(set.obs);
      spn_dag_digest_t* digests = sp_alloc_n(s.mem, spn_dag_digest_t, count);
      bool resolved = !resolve_observations(env->files, set.obs, count, digests);
      spn_dag_digest_t strong = resolved ? spn_dag_strong_key(attempt->key, set.pinned, set.obs, digests, count) : attempt->key;
      sp_mem_end_scratch(s);
      trace_resolve(env, action->id, resolved);
      if (!resolved) {
        break;
      }
      trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_STRONG, .action = action->id, .key = strong });
      attempt->hit = try_restore(g, action, strong, env);
      break;
    }
    case SPN_DAG_ACTION_UNCACHEABLE: {
      break;
    }
  }
}

static spn_err_t execute(spn_dag_t* g, spn_dag_attempt_t* attempt, spn_dag_env_t* env) {
  spn_dag_action_t* action = attempt->action;
  sp_assert(!spn_path_empty(env->scratch));
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = SPN_OK;

  spn_path_t scratch = scratch_dir(s.mem, env, action);
  sp_str_t dir = spn_path_str(g->roots, s.mem, scratch);
  sp_fs_remove_dir(dir);
  if (sp_fs_create_dir(dir)) {
    err = SPN_ERR_DAG_SCRATCH;
    goto done;
  }

  spn_path_t* outputs = sp_alloc_n(s.mem, spn_path_t, sp_da_size(action->produces));
  sp_da_for(action->produces, it) {
    spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    outputs[it] = spn_path_join(s.mem, scratch, artifact->name);
    if (artifact->kind == SPN_DAG_ARTIFACT_KIND_TREE) {
      sp_fs_create_dir(spn_path_str(g->roots, s.mem, outputs[it]));
    }
  }

  trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_EXECUTE, .action = action->id, .key = attempt->key });

  err = action->execute(g, action, action->user_data, env, outputs, &attempt->obs);
  if (err) {
    diag_set(&attempt->diag, err, action->id, sp_str_lit(""));
    goto done;
  }
  switch (action->kind) {
    case SPN_DAG_ACTION_DISCOVERED: {
      spn_dag_obs_canonicalize(attempt->obs.rows);
      break;
    }
    case SPN_DAG_ACTION_STATIC:
    case SPN_DAG_ACTION_UNCACHEABLE: {
      sp_assert(sp_da_empty(attempt->obs.rows));
      break;
    }
  }

  sp_da_for(action->produces, it) {
    spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    sp_str_t produced = spn_path_str(g->roots, s.mem, outputs[it]);
    err = SPN_ERR_DAG_MISSING_OUTPUT;
    if (sp_fs_exists(produced)) {
      err = artifact->kind == SPN_DAG_ARTIFACT_KIND_TREE
        ? spn_dag_store_put_tree(env->store, produced, &attempt->digests[it])
        : spn_dag_store_put_file(env->store, produced, artifact->name, &attempt->digests[it]);
    }
    if (err) {
      diag_set(&attempt->diag, err, action->id, artifact_location(g, artifact));
      goto done;
    }
  }

done:
  sp_mem_end_scratch(s);
  return err;
}

static spn_err_t commit(spn_dag_t* g, spn_dag_attempt_t* attempt, spn_dag_env_t* env) {
  spn_dag_action_t* action = attempt->action;
  sp_assert(!attempt->hit);

  sp_da_for(action->produces, it) {
    spn_dag_find_artifact(g, action->produces[it])->digest = attempt->digests[it];
  }

  switch (action->kind) {
    case SPN_DAG_ACTION_UNCACHEABLE: {
      spn_try(settle(g, action, env, &attempt->diag));
      trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_COMMIT, .action = action->id });
      return SPN_OK;
    }
    case SPN_DAG_ACTION_STATIC: {
      spn_try(settle(g, action, env, &attempt->diag));
      record(g, action, attempt->key, env);
      trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_COMMIT, .action = action->id, .key = attempt->key, .hit = true });
      return SPN_OK;
    }
    case SPN_DAG_ACTION_DISCOVERED: {
      spn_dag_pathset_t set = spn_dag_obs_table_put(env->discovery, attempt->key, &attempt->obs);
      sp_mem_arena_marker_t s = sp_mem_begin_scratch();
      u32 count = (u32)sp_da_size(set.obs);
      spn_dag_digest_t* digests = sp_alloc_n(s.mem, spn_dag_digest_t, count);
      bool resolved = !resolve_observations(env->files, set.obs, count, digests);
      spn_dag_digest_t key = resolved ? spn_dag_strong_key(attempt->key, set.pinned, set.obs, digests, count) : attempt->key;
      sp_mem_end_scratch(s);
      trace_resolve(env, action->id, resolved);
      if (resolved) {
        trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_STRONG, .action = action->id, .key = key });
      }
      spn_try(settle(g, action, env, &attempt->diag));
      if (resolved) {
        record(g, action, key, env);
      }
      trace_emit(env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_COMMIT, .action = action->id, .key = key, .hit = resolved });
      return SPN_OK;
    }
  }

  sp_unreachable_return(SPN_ERROR);
}

static spn_err_t exec_action(spn_dag_t* g, spn_dag_action_t* action, spn_dag_env_t* env) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  env->diag = (spn_dag_diag_t) sp_zero;

  spn_dag_attempt_t attempt = {
    .digests = sp_alloc_n(s.mem, spn_dag_digest_t, sp_da_size(action->produces)),
    .obs.table = env->discovery,
  };
  lookup(g, action, env, &attempt);
  spn_err_t err = SPN_OK;
  if (!attempt.hit) {
    err = execute(g, &attempt, env);
    if (!err) {
      err = commit(g, &attempt, env);
    }
    attempt_discard(g, env, &attempt);
  }
  diag_flush(env, &attempt, err);
  if (!err) {
    progress_count(env, action, attempt.hit);
  }

  sp_mem_end_scratch(s);
  return err;
}

spn_err_t spn_dag_execute(spn_dag_t* g, spn_dag_id_t action_id, spn_dag_env_t* env) {
  spn_dag_action_t* action = spn_dag_find_action(g, action_id);
  sp_assert(action->kind != SPN_DAG_ACTION_DISCOVERED || env->discovery);
  return exec_action(g, action, env);
}

typedef struct {
  u32 producer;
  spn_dag_artifact_kind_t kind;
} spn_dag_target_t;

typedef struct {
  sp_ht(spn_path_t, spn_dag_target_t) by_path;
  sp_ht(spn_path_t, sp_da(u32)) below;
} spn_dag_targets_t;

static bool path_parent(spn_path_t* path) {
  if (sp_str_empty(path->sub)) {
    return false;
  }
  path->sub = parent_dir(path->sub);
  return !sp_str_empty(path->sub) || path->root != SPN_PATH_ROOT_NONE;
}

static void targets_init(spn_dag_targets_t* targets, spn_dag_t* g, sp_mem_t mem) {
  sp_ht_init(mem, targets->by_path);
  sp_ht_set_fns(targets->by_path, spn_path_on_hash, spn_path_on_compare);
  sp_ht_init(mem, targets->below);
  sp_ht_set_fns(targets->below, spn_path_on_hash, spn_path_on_compare);

  sp_da_for(g->artifacts, it) {
    spn_dag_artifact_t* artifact = &g->artifacts[it];
    if (!artifact->producer.occupied || spn_path_empty(artifact->path)) {
      continue;
    }

    sp_assert(!(g->roots->pinned & spn_path_root_mask(artifact->path.root)));
    sp_assert(!sp_ht_getp(targets->by_path, artifact->path));
    sp_ht_insert(targets->by_path, artifact->path, ((spn_dag_target_t) {
      .producer = artifact->producer.index,
      .kind = artifact->kind
    }));

    for (spn_path_t dir = artifact->path; path_parent(&dir);) {
      sp_da(u32)* below = sp_ht_getp(targets->below, dir);
      if (!below) {
        sp_ht_insert(targets->below, dir, sp_da_new(mem, u32));
        below = sp_ht_getp(targets->below, dir);
      }
      sp_da_push(*below, artifact->producer.index);
    }
  }
}

typedef struct spn_dag_run_t spn_dag_run_t;

typedef struct {
  spn_dag_run_t* run;
  spn_dag_action_t* action;
  u64 epoch;
  spn_err_t err;
  spn_dag_attempt_t attempt;
} spn_dag_flight_t;

typedef struct {
  u32 pending;
  u32 deferred;
  bool done;
  bool parked;
  u64 done_epoch;
  sp_da(u32) waiters;
  spn_dag_flight_t flight;
} spn_dag_run_state_t;

struct spn_dag_run_t {
  spn_dag_t* g;
  spn_dag_env_t* env;
  spn_thread_pool_executor_t* ex;
  spn_dag_targets_t targets;
  spn_dag_run_state_t* states;
  sp_da(spn_dag_id_t) ready;
  sp_atomic_s32_t completed;
  u32 in_flight;
  spn_err_t err;
};

static void defer_producer(spn_dag_run_t* run, spn_dag_action_t* action, u32 producer_index, u64 epoch, bool* requeue) {
  if (producer_index == action->id.index) {
    return;
  }
  spn_dag_run_state_t* producer = &run->states[producer_index];
  if (producer->done) {
    if (producer->done_epoch > epoch && run->g->actions[producer_index].wrote) {
      trace_emit(run->env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_REQUEUE, .action = action->id, .producer = run->g->actions[producer_index].id });
      *requeue = true;
    }
    return;
  }
  trace_emit(run->env, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_DEFER, .action = action->id, .producer = run->g->actions[producer_index].id });
  sp_da_push(producer->waiters, action->id.index);
  run->states[action->id.index].deferred++;
}

static bool defer_observations(spn_dag_run_t* run, spn_dag_action_t* action, sp_da(spn_dag_obs_t) obs, u64 epoch, bool* requeue) {
  sp_da_for(obs, it) {
    const spn_dag_obs_t* o = &obs[it];
    if (run->g->roots->pinned & spn_path_root_mask(o->path.root)) {
      continue;
    }

    spn_dag_target_t* exact = sp_ht_getp(run->targets.by_path, o->path);
    if (exact) {
      defer_producer(run, action, exact->producer, epoch, requeue);
    }

    for (spn_path_t dir = o->path; path_parent(&dir);) {
      spn_dag_target_t* tree = sp_ht_getp(run->targets.by_path, dir);
      if (tree && tree->kind == SPN_DAG_ARTIFACT_KIND_TREE) {
        defer_producer(run, action, tree->producer, epoch, requeue);
      }
    }

    if (o->kind == SPN_DAG_OBS_ENUMERATION) {
      sp_da(u32)* below = sp_ht_getp(run->targets.below, o->path);
      if (below) {
        sp_da_for(*below, bi) {
          defer_producer(run, action, (*below)[bi], epoch, requeue);
        }
      }
    }
  }

  return run->states[action->id.index].deferred > 0;
}

static spn_err_t seed_source(spn_dag_env_t* env, spn_dag_artifact_t* artifact) {
  artifact->materialized = artifact->path;
  sp_sys_file_meta_t sys = sp_zero;
  spn_try(spn_dag_file_cache_stat(env->files, artifact->materialized, &sys));
  if (sys.kind == SP_FS_KIND_DIR) {
    return SPN_ERR_DAG_MISSING_INPUT;
  }
  return spn_dag_file_cache_digest(env->files, artifact->materialized, &artifact->digest);
}

static spn_err_t seed_sources(spn_dag_t* g, spn_dag_env_t* env) {
  sp_da_for(g->artifacts, it) {
    spn_dag_artifact_t* artifact = &g->artifacts[it];
    switch (artifact->kind) {
      case SPN_DAG_ARTIFACT_KIND_TREE: {
        break;
      }
      case SPN_DAG_ARTIFACT_KIND_FILE: {
        if (!artifact->producer.occupied) {
          if (g->roots->pinned & spn_path_root_mask(artifact->path.root)) {
            artifact->materialized = artifact->path;
            artifact->digest = spn_dag_path_digest(artifact->path);
          }
          else if (seed_source(env, artifact)) {
            diag_set(&env->diag, SPN_ERR_DAG_MISSING_INPUT, (spn_dag_id_t) sp_zero, artifact_render(g, artifact->path));
            return SPN_ERR_DAG_MISSING_INPUT;
          }
        }
        break;
      }
      case SPN_DAG_ARTIFACT_KIND_VALUE: {
        break;
      }
    }
  }
  return SPN_OK;
}

static void seed_ready(spn_dag_run_t* run, sp_mem_t mem) {
  u64 outputs = 0;
  sp_da_for(run->g->actions, ai) {
    outputs += sp_da_size(run->g->actions[ai].produces);
  }
  spn_dag_digest_t* digests = sp_alloc_n(mem, spn_dag_digest_t, outputs);

  sp_da_for(run->g->actions, ai) {
    spn_dag_action_t* action = &run->g->actions[ai];
    sp_da_init(mem, run->states[ai].waiters);
    run->states[ai].flight = (spn_dag_flight_t) {
      .run = run,
      .action = action,
      .attempt = { .digests = digests },
    };
    digests += sp_da_size(action->produces);
    sp_da_for(action->consumes, ci) {
      if (!spn_dag_digest_valid(spn_dag_find_artifact(run->g, action->consumes[ci])->digest)) {
        run->states[ai].pending++;
      }
    }
    if (!run->states[ai].pending) {
      sp_da_push(run->ready, action->id);
    }
  }
}

static void finish_action(spn_dag_run_t* run, spn_dag_action_t* action) {
  spn_dag_run_state_t* state = &run->states[action->id.index];
  state->done = true;
  state->done_epoch = (u64)(sp_atomic_s32_add(&run->completed, 1, SP_ATOMIC_SEQ_CST) + 1);

  sp_da_for(action->produces, pi) {
    spn_dag_artifact_t* produced = spn_dag_find_artifact(run->g, action->produces[pi]);
    sp_da_for(produced->consumers, cj) {
      spn_dag_run_state_t* consumer = &run->states[produced->consumers[cj].index];
      if (consumer->pending) {
        consumer->pending--;
        if (!consumer->pending) {
          sp_da_push(run->ready, produced->consumers[cj]);
        }
      }
    }
  }

  sp_da_for(state->waiters, wi) {
    u32 index = state->waiters[wi];
    spn_dag_run_state_t* waiter = &run->states[index];
    if (waiter->deferred) {
      waiter->deferred--;
      if (!waiter->deferred) {
        sp_da_push(run->ready, run->g->actions[index].id);
      }
    }
  }
}

static void flight_run(void* data) {
  spn_dag_flight_t* flight = (spn_dag_flight_t*)data;
  spn_dag_run_t* run = flight->run;
  flight->epoch = (u64)sp_atomic_s32_load(&run->completed, SP_ATOMIC_SEQ_CST);
  lookup(run->g, flight->action, run->env, &flight->attempt);
  if (!flight->attempt.hit) {
    flight->err = execute(run->g, &flight->attempt, run->env);
  }
}

static void run_commit_flight(spn_dag_run_t* run, spn_dag_action_t* action, spn_dag_flight_t* flight) {
  run->err = commit(run->g, &flight->attempt, run->env);
  diag_flush(run->env, &flight->attempt, run->err);
  attempt_discard(run->g, run->env, &flight->attempt);
  if (run->err) {
    return;
  }
  progress_count(run->env, action, false);
  finish_action(run, action);
}

static void run_dispatch(spn_dag_run_t* run, spn_dag_id_t id) {
  spn_dag_action_t* action = spn_dag_find_action(run->g, id);
  spn_dag_run_state_t* state = &run->states[id.index];
  spn_dag_flight_t* flight = &state->flight;

  if (state->parked) {
    bool requeue = false;
    if (defer_observations(run, action, flight->attempt.obs.rows, flight->epoch, &requeue)) {
      return;
    }
    state->parked = false;
    if (!requeue) {
      run_commit_flight(run, action, flight);
      return;
    }
    attempt_discard(run->g, run->env, &flight->attempt);
  }

  if (action->kind == SPN_DAG_ACTION_DISCOVERED) {
    sp_assert(run->env->discovery);
    spn_dag_pathset_t set = sp_zero;
    bool requeue = false;
    if (spn_dag_obs_table_get(run->env->discovery, spn_dag_weak_key(run->g, action->id), &set)
      && defer_observations(run, action, set.obs, (u64)sp_atomic_s32_load(&run->completed, SP_ATOMIC_SEQ_CST), &requeue)) {
      return;
    }
    sp_assert(!requeue);
  }

  flight->err = SPN_OK;
  flight->attempt = (spn_dag_attempt_t) {
    .digests = flight->attempt.digests,
    .obs.table = run->env->discovery,
  };

  spn_thread_pool_submit(run->ex, (spn_thread_pool_job_t) { .fn = flight_run, .data = flight });
  run->in_flight++;
}

static void run_complete(spn_dag_run_t* run, spn_dag_flight_t* flight) {
  spn_dag_action_t* action = flight->action;
  spn_dag_attempt_t* attempt = &flight->attempt;

  if (run->err || flight->err) {
    diag_flush(run->env, attempt, flight->err);
    run->err = run->err ? run->err : flight->err;
    if (!attempt->hit) {
      attempt_discard(run->g, run->env, attempt);
    }
    return;
  }

  if (attempt->hit) {
    progress_count(run->env, action, true);
    finish_action(run, action);
    return;
  }

  if (action->kind == SPN_DAG_ACTION_DISCOVERED) {
    bool requeue = false;
    if (defer_observations(run, action, attempt->obs.rows, flight->epoch, &requeue)) {
      run->states[action->id.index].parked = true;
      return;
    }
    if (requeue) {
      attempt_discard(run->g, run->env, attempt);
      sp_da_push(run->ready, action->id);
      return;
    }
  }

  run_commit_flight(run, action, flight);
}

spn_err_t spn_dag_run_executor(spn_dag_t* g, spn_dag_env_t* env, spn_thread_pool_executor_t* ex) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  env->diag = (spn_dag_diag_t) sp_zero;

  sp_str_t scratch = spn_path_str(g->roots, s.mem, spn_path_join(s.mem, env->scratch, sp_str_lit("scratch")));
  sp_fs_create_dir(scratch);
  spn_dag_run_t run = {
    .g = g,
    .env = env,
    .ex = ex,
    .err = spn_dag_file_cache_fence_dir(env->files, scratch),
  };
  if (run.err) {
    diag_set(&env->diag, run.err, (spn_dag_id_t) sp_zero, sp_str_lit(""));
  }
  if (!run.err) {
    spn_dag_violation_t violation = spn_dag_validate(g);
    run.err = violation.err;
    if (violation.err) {
      diag_set(&env->diag, violation.err, (spn_dag_id_t) sp_zero, artifact_render(g, violation.path));
    }
  }
  if (!run.err) {
    run.err = seed_sources(g, env);
  }

  u64 n = sp_da_size(g->actions);
  sp_assert(n < (1u << 30));
  if (!run.err) {
    progress_total(env, n);
    run.states = sp_alloc_n(s.mem, spn_dag_run_state_t, n ? n : 1);
    run.ready = sp_da_new(s.mem, spn_dag_id_t);
    seed_ready(&run, s.mem);
    targets_init(&run.targets, g, s.mem);

    u64 turns = 0;
    u64 turns_max = 4 * (n + 1) * (n + 1);
    while (true) {
      turns++;
      sp_assert(turns <= turns_max);

      if (!run.err && env->cancel && sp_atomic_s32_load(env->cancel, SP_ATOMIC_SEQ_CST)) {
        run.err = SPN_ERR_DAG_CANCELLED;
      }

      spn_thread_pool_job_t job = spn_thread_pool_try_poll(ex);
      if (job.fn) {
        run.in_flight--;
        run_complete(&run, (spn_dag_flight_t*)job.data);
        continue;
      }
      if (!run.err && !sp_da_empty(run.ready)) {
        spn_dag_id_t id = *sp_da_back(run.ready);
        sp_da_pop(run.ready);
        run_dispatch(&run, id);
        continue;
      }
      if (run.in_flight) {
        job = spn_thread_pool_poll(ex);
        run.in_flight--;
        run_complete(&run, (spn_dag_flight_t*)job.data);
        continue;
      }
      break;
    }

    sp_for(it, n) {
      if (run.states[it].parked) {
        attempt_discard(g, env, &run.states[it].flight.attempt);
      }
    }
    if (!run.err && (u64)sp_atomic_s32_load(&run.completed, SP_ATOMIC_SEQ_CST) != n) {
      run.err = SPN_ERR_DAG_STALLED;
      diag_set(&env->diag, SPN_ERR_DAG_STALLED, (spn_dag_id_t) sp_zero, sp_str_lit(""));
    }
  }

  sp_mem_end_scratch(s);
  return run.err;
}

spn_err_t spn_dag_run(spn_dag_t* g, spn_dag_env_t* env) {
  spn_thread_pool_t pool = sp_zero;
  spn_thread_pool_init(&pool, g->mem, (spn_thread_pool_config_t) sp_zero);
  spn_err_t err = spn_dag_run_executor(g, env, &pool.executor);
  spn_thread_pool_deinit(&pool);
  return err;
}
