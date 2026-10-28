#include "dag/dag.h"
#include "dag/node.h"
#include "dag/stamp.h"
#include "dag/types.h"
#include "core/core.h"
#include "paths/paths.h"
#include "thread_pool/thread_pool.h"
#include "hash/digest/digest.h"
#include "sp.h"
#include "macro/macro.h"
#include "spn/core.h"
#include "fs/fs.h"
#include "sp/sp_glob.h"
#include "str/str.h"


static bool is_timespec_equal(sp_sys_timespec_t a, sp_sys_timespec_t b) {
  return a.tv_sec == b.tv_sec && a.tv_nsec == b.tv_nsec;
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
  sp_ht_init(c->mem, c->canonical);
  sp_ht_set_fns(c->canonical, spn_path_on_hash, spn_path_on_compare);
}

spn_path_t spn_dag_file_cache_canonical(spn_dag_file_cache_t* c, spn_path_t path) {
  sp_mutex_lock(&c->mutex);
  spn_path_t* cached = sp_ht_getp(c->canonical, path);
  if (cached) {
    spn_path_t result = *cached;
    sp_mutex_unlock(&c->mutex);
    return result;
  }
  sp_mutex_unlock(&c->mutex);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_path_t canonical = spn_path_canonicalize_head(s.mem, c->roots, path);

  sp_mutex_lock(&c->mutex);
  canonical = spn_path_copy(c->mem, canonical);
  sp_ht_insert(c->canonical, spn_path_copy(c->mem, path), canonical);
  sp_mutex_unlock(&c->mutex);
  sp_mem_end_scratch(s);
  return canonical;
}

void spn_dag_file_cache_fence(spn_dag_file_cache_t* c, sp_sys_timespec_t fence) {
  sp_mutex_lock(&c->mutex);
  c->stamp = (spn_dag_stamp_t) { .fence = fence };
  sp_mutex_unlock(&c->mutex);
}

spn_err_t spn_dag_file_cache_fence_dir(spn_dag_file_cache_t* c, spn_path_t dir) {
  sp_sys_timespec_t fence = sp_zero;
  spn_try(spn_dag_stamp_probe(spn_path_at(c->roots, dir), &fence));

  sp_mutex_lock(&c->mutex);
  c->stamp = (spn_dag_stamp_t) {
    .fence = fence,
    .dir = spn_path_copy(c->mem, dir)
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
  sp_path_t at = spn_path_at(c->roots, path);
  sp_sys_file_meta_t sys = sp_zero;
  if (sp_sys_get_path_metadata_s(at.dir, at.sub, &sys)) {
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
  spn_err_t admitted = spn_dag_stamp_admit(&c->stamp, c->roots, fresh.mtime, &record);
  sp_mutex_unlock(&c->mutex);
  spn_try(admitted);

  u64 size = 0;
  spn_try(spn_digest_file(SPN_DIGEST_BLAKE3, spn_path_at(c->roots, path), digest->bytes, &size));
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
  file_cache_record(c, path, meta);
  sp_mutex_unlock(&c->mutex);
  return SPN_OK;
}

const spn_dag_artifact_state_t* spn_dag_run_artifact(const spn_dag_run_t* run, spn_dag_id_t id) {
  sp_assert(id.occupied);
  sp_assert(id.graph == run->g->id);
  sp_assert(id.index < sp_da_size(run->g->artifacts));
  return run->artifacts + id.index;
}

static spn_dag_artifact_state_t* artifact_state(spn_dag_run_t* run, spn_dag_id_t id) {
  return sp_ptr_cast(spn_dag_artifact_state_t*, spn_dag_run_artifact(run, id));
}

struct si_dag_action_handle {
  spn_dag_run_t* run;
  const spn_dag_action_t* action;
  spn_dag_digest_t key;
  bool hit;
  bool wrote;
  spn_dag_digest_t* digests;
  spn_path_t* outputs;
  spn_dag_obs_set_t obs;
  spn_dag_diag_t diag;
};

static void diag_set(spn_dag_diag_t* diag, spn_err_t err, spn_dag_id_t action, sp_str_t path) {
  if (!diag || diag->err) {
    return;
  }
  diag->err = err;
  diag->action = action;
  diag->path = path;
}

static void trace_emit(spn_dag_run_t* run, spn_dag_trace_event_t event) {
  if (run->trace) {
    run->trace(&event, run->trace_data);
  }
}

static void trace_resolve(spn_dag_run_t* run, spn_dag_id_t action, bool hit) {
  trace_emit(run, (spn_dag_trace_event_t) {
    .kind = SPN_DAG_TRACE_RESOLVE,
    .action = action,
    .hit = hit
  });
}

static void progress_wake(spn_dag_run_t* run) {
  if (run->wake) {
    spn_wake_ring(run->wake);
  }
}

static void progress_begin(spn_dag_run_t* run, u64 total) {
  sp_atomic_s32_store(&run->progress.total, (s32)total, SP_ATOMIC_SEQ_CST);
  sp_atomic_s32_store(&run->progress.completed, 0, SP_ATOMIC_SEQ_CST);
  sp_atomic_s32_store(&run->progress.hits, 0, SP_ATOMIC_SEQ_CST);
  sp_atomic_s32_store(&run->progress.misses, 0, SP_ATOMIC_SEQ_CST);
  sp_atomic_u64_store(&run->progress.warm, 0, SP_ATOMIC_SEQ_CST);
  progress_wake(run);
}

static void progress_count(spn_dag_run_t* run, const spn_dag_action_t* action, bool hit) {
  switch (action->kind) {
    case SPN_DAG_ACTION_STATIC:
    case SPN_DAG_ACTION_DISCOVERED: {
      sp_atomic_s32_add(hit ? &run->progress.hits : &run->progress.misses, 1, SP_ATOMIC_SEQ_CST);
      break;
    }
    case SPN_DAG_ACTION_UNCACHEABLE: {
      sp_assert(!hit);
      break;
    }
  }
  sp_atomic_s32_add(&run->progress.completed, 1, SP_ATOMIC_SEQ_CST);
  progress_wake(run);
}

static sp_str_t render_path(spn_dag_run_t* run, spn_path_t path) {
  sp_mutex_lock(&run->mutex);
  sp_str_t rendered = spn_path_str(run->g->roots, run->mem, path);
  sp_mutex_unlock(&run->mutex);
  return rendered;
}

static void diag_set_path(si_dag_action_handle_t* a, spn_dag_diag_t* diag, spn_err_t err, spn_path_t path) {
  if (!diag || diag->err) {
    return;
  }
  diag_set(diag, err, a->action->id, render_path(a->run, path));
}

static sp_str_t artifact_location(si_dag_action_handle_t* a, const spn_dag_artifact_t* artifact) {
  return spn_path_empty(artifact->path) ? artifact->name : render_path(a->run, artifact->path);
}

typedef enum {
  TARGET_SETTLED,
  TARGET_STALE,
  TARGET_POISONED,
} target_state_t;

static target_state_t target_state(spn_dag_run_t* run, spn_path_t path, sp_str_t name, spn_dag_digest_t digest) {
  sp_sys_file_meta_t sys = sp_zero;
  spn_dag_digest_t existing = sp_zero;
  if (spn_dag_file_cache_stat(run->files, path, &sys) || spn_dag_file_cache_digest(run->files, path, &existing)) {
    return TARGET_STALE;
  }
  if (spn_dag_digest_equal(existing, digest)) {
    return TARGET_SETTLED;
  }
  return spn_dag_store_owns(run->store, digest, name, sys) ? TARGET_POISONED : TARGET_STALE;
}

static spn_err_t link_target(spn_dag_run_t* run, spn_path_t path, sp_str_t name, spn_dag_digest_t digest) {
  spn_try(spn_dag_store_materialize(run->store, digest, name, path));
  return spn_dag_file_cache_seed(run->files, path, digest);
}

typedef struct {
  sp_str_t rel;
  sp_fs_kind_t kind;
} extra_entry_t;

static spn_err_t settle_tree(si_dag_action_handle_t* a, const spn_dag_artifact_t* artifact, spn_dag_diag_t* diag) {
  spn_dag_run_t* run = a->run;
  const spn_dag_t* g = run->g;
  const spn_dag_action_t* action = a->action;
  spn_dag_digest_t digest = spn_dag_run_artifact(run, artifact->id)->digest;
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = SPN_OK;

  sp_da(spn_dag_action_output_t) entries = sp_zero;
  err = spn_dag_tree_entries(run->store, digest, s.mem, &entries);
  if (err) {
    diag_set_path(a, diag, err, artifact->path);
    goto done;
  }
  spn_path_t* paths = sp_alloc_n(s.mem, spn_path_t, sp_da_size(entries) ? sp_da_size(entries) : 1);
  sp_da_for(entries, it) {
    paths[it] = spn_path_join(s.mem, artifact->path, entries[it].name);
  }

  sp_path_t dir = spn_path_at(g->roots, artifact->path);
  sp_sys_file_meta_t meta = sp_zero;
  bool exists = !sp_sys_get_path_metadata_s(dir.dir, dir.sub, &meta) && meta.kind == SP_FS_KIND_DIR;
  sp_da(extra_entry_t) extra = sp_da_new(s.mem, extra_entry_t);
  sp_da(u32) stale = sp_da_new(s.mem, u32);
  sp_da(u32) poisoned = sp_da_new(s.mem, u32);
  u64 files = 0;
  if (exists) {
    sp_str_ht(bool) keep_files = SP_NULLPTR;
    sp_str_ht(bool) keep_dirs = SP_NULLPTR;
    sp_str_ht_init(s.mem, keep_files);
    sp_str_ht_init(s.mem, keep_dirs);
    sp_da_for(entries, it) {
      sp_str_ht_insert(keep_files, entries[it].name, true);
      sp_str_t parent = entries[it].name;
      for (s32 sep = sp_str_find_c8_reverse(parent, '/'); sep != SP_STR_NO_MATCH; sep = sp_str_find_c8_reverse(parent, '/')) {
        parent = sp_str_prefix(parent, sep);
        sp_str_ht_insert(keep_dirs, parent, true);
      }
    }

    sp_fs_it_t walk = sp_fs_it_new_at(s.mem, dir, 0);
    while (sp_fs_it_walk(&walk)) {
      sp_fs_entry_t entry = walk.entry;
      files += entry.kind != SP_FS_KIND_DIR;
      if (entry.kind == SP_FS_KIND_DIR ? sp_str_ht_exists(keep_dirs, entry.rel) : sp_str_ht_exists(keep_files, entry.rel)) {
        continue;
      }
      sp_da_push(extra, ((extra_entry_t) { .rel = sp_str_copy(s.mem, entry.rel), .kind = entry.kind }));
    }
    sp_fs_it_deinit(&walk);
    if (walk.err) {
      err = SPN_ERR_DAG_STORE_READ;
      diag_set_path(a, diag, err, artifact->path);
      goto done;
    }
    sp_da_for(entries, it) {
      switch (target_state(run, paths[it], entries[it].name, entries[it].digest)) {
        case TARGET_SETTLED:  break;
        case TARGET_STALE:    sp_da_push(stale, (u32)it); break;
        case TARGET_POISONED: sp_da_push(poisoned, (u32)it); break;
      }
    }
  }
  else {
    if (sp_fs_create_dir_at(dir)) {
      err = SPN_ERR_DAG_STORE_WRITE;
      diag_set_path(a, diag, err, artifact->path);
      goto done;
    }
    sp_da_for(entries, it) {
      sp_da_push(stale, (u32)it);
    }
  }

  if (exists && sp_da_empty(stale) && sp_da_empty(poisoned) && files == sp_da_size(entries)) {
    trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = digest, .hit = true });
    goto done;
  }
  a->wrote = true;

  sp_da_for(poisoned, it) {
    spn_dag_store_drop(run->store, entries[poisoned[it]].digest, entries[poisoned[it]].name);
  }
  if (!sp_da_empty(poisoned)) {
    err = SPN_ERR_DAG_STORE_MISSING;
    diag_set_path(a, diag, err, paths[poisoned[0]]);
    goto done;
  }

  sp_da_rfor(extra, it) {
    spn_path_t path = spn_path_join(s.mem, artifact->path, extra[it].rel);
    sp_path_t at = spn_path_at(g->roots, path);
    sp_err_t removed = extra[it].kind == SP_FS_KIND_DIR ? sp_fs_remove_dir_at(at) : sp_fs_remove_file_at(at);
    if (removed) {
      err = SPN_ERR_DAG_STORE_WRITE;
      diag_set_path(a, diag, err, path);
      goto done;
    }
    spn_dag_file_cache_invalidate(run->files, path);
  }

  sp_da_for(stale, it) {
    spn_dag_action_output_t* entry = &entries[stale[it]];
    err = link_target(run, paths[stale[it]], entry->name, entry->digest);
    if (err) {
      diag_set_path(a, diag, err, paths[stale[it]]);
      goto done;
    }
  }
  trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = digest, .hit = false });

done:
  sp_mem_end_scratch(s);
  return err;
}

static spn_err_t settle_file(si_dag_action_handle_t* a, const spn_dag_artifact_t* artifact, spn_dag_diag_t* diag) {
  spn_dag_run_t* run = a->run;
  const spn_dag_action_t* action = a->action;
  spn_dag_digest_t digest = spn_dag_run_artifact(run, artifact->id)->digest;
  switch (target_state(run, artifact->path, artifact->name, digest)) {
    case TARGET_SETTLED: {
      trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = digest, .hit = true });
      return SPN_OK;
    }
    case TARGET_POISONED: {
      spn_dag_store_drop(run->store, digest, artifact->name);
      diag_set_path(a, diag, SPN_ERR_DAG_STORE_MISSING, artifact->path);
      return SPN_ERR_DAG_STORE_MISSING;
    }
    case TARGET_STALE: {
      break;
    }
  }
  a->wrote = true;
  spn_err_t err = link_target(run, artifact->path, artifact->name, digest);
  if (err) {
    diag_set_path(a, diag, err, artifact->path);
    return err;
  }
  trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = digest, .hit = false });
  return SPN_OK;
}

static spn_err_t settle(si_dag_action_handle_t* a, spn_dag_diag_t* diag) {
  const spn_dag_action_t* action = a->action;
  spn_dag_run_t* run = a->run;
  a->wrote = false;
  sp_da_for(action->produces, it) {
    const spn_dag_artifact_t* artifact = spn_dag_find_artifact(run->g, action->produces[it]);
    spn_dag_artifact_state_t* produced = artifact_state(run, artifact->id);
    if (spn_path_empty(artifact->path)) {
      sp_mutex_lock(&run->mutex);
      spn_err_t located = spn_dag_store_locate(run->store, run->mem, produced->digest, artifact->name, &produced->location);
      sp_mutex_unlock(&run->mutex);
      if (located) {
        diag_set(diag, located, action->id, artifact->name);
        return located;
      }
      continue;
    }
    spn_try(artifact->kind == SPN_DAG_ARTIFACT_KIND_TREE
      ? settle_tree(a, artifact, diag)
      : settle_file(a, artifact, diag));
    produced->location = artifact->path;
  }
  return SPN_OK;
}

static bool restore_entry(si_dag_action_handle_t* a, const spn_dag_action_entry_t* entry) {
  const spn_dag_action_t* action = a->action;
  if (sp_da_size(entry->outputs) != sp_da_size(action->produces)) {
    return false;
  }

  sp_da_for(entry->outputs, it) {
    if (!sp_str_equal(entry->outputs[it].name, spn_dag_find_artifact(a->run->g, action->produces[it])->name)) {
      return false;
    }
  }

  sp_da_for(entry->outputs, it) {
    artifact_state(a->run, action->produces[it])->digest = entry->outputs[it].digest;
  }

  return !settle(a, SP_NULLPTR);
}

static bool try_restore(si_dag_action_handle_t* a, spn_dag_digest_t key) {
  spn_dag_run_t* run = a->run;
  spn_dag_action_entry_t entry = sp_zero;
  bool present = spn_dag_action_cache_get(run->cache, key, &entry);
  bool hit = present && restore_entry(a, &entry);
  trace_emit(run, (spn_dag_trace_event_t) {
    .kind = SPN_DAG_TRACE_CACHE,
    .action = a->action->id,
    .key = key,
    .present = present,
    .hit = hit,
  });
  if (present && !hit) {
    spn_dag_action_cache_remove(run->cache, key);
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

static spn_err_t membership_digest(sp_path_t dir, sp_str_t filter, spn_dag_digest_t* digest) {
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
  sp_fs_it_t walk = sp_fs_it_new_at(s.mem, dir, 0);
  while (sp_fs_it_next(&walk)) {
    if (walk.entry.kind != SP_FS_KIND_DIR && glob && !sp_glob_match(glob, walk.entry.name)) {
      continue;
    }
    sp_da_push(members, ((sp_fs_entry_t) { .name = sp_str_copy(s.mem, walk.entry.name), .kind = walk.entry.kind }));
  }
  sp_fs_it_deinit(&walk);
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
      return membership_digest(spn_path_at(files->roots, o->path), o->filter, digest);
    }
    case SPN_DAG_OBS_ABSENT: {
      sp_path_t at = spn_path_at(files->roots, o->path);
      sp_sys_file_meta_t sys = sp_zero;
      sp_err_t rc = sp_sys_get_path_metadata_s(at.dir, at.sub, &sys);
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
    return membership_digest(spn_path_at(files->roots, o->path), sp_str_lit(""), digest);
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

static void record(si_dag_action_handle_t* a, spn_dag_digest_t key) {
  const spn_dag_action_t* action = a->action;
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();

  sp_da(spn_dag_action_output_t) outputs = sp_da_new(s.mem, spn_dag_action_output_t);
  sp_da_for(action->produces, it) {
    sp_da_push(outputs, ((spn_dag_action_output_t) {
      .name = spn_dag_find_artifact(a->run->g, action->produces[it])->name,
      .digest = spn_dag_run_artifact(a->run, action->produces[it])->digest
    }));
  }
  spn_dag_action_cache_put(a->run->cache, key, outputs, (u32)sp_da_size(outputs));

  sp_mem_end_scratch(s);
}

spn_path_t si_dag_resolve_input(const si_dag_ctx_t* c, spn_dag_id_t id) {
  return spn_dag_run_artifact(c->handle->run, id)->location;
}

spn_path_t si_dag_resolve_output(const si_dag_ctx_t* c, spn_dag_id_t id) {
  sp_da_for(c->produces, it) {
    if (c->produces[it].index == id.index) {
      return c->handle->outputs[it];
    }
  }
  sp_unreachable_return(sp_zero_s(spn_path_t));
}

spn_path_t si_dag_canonicalize_path(const si_dag_ctx_t* c, spn_path_t path) {
  return spn_dag_file_cache_canonical(c->handle->run->files, path);
}

void si_dag_observe(const si_dag_ctx_t* c, spn_dag_obs_t obs) {
  spn_dag_observe(c->obs, obs);
}

void si_dag_add_warm(const si_dag_ctx_t* c, s64 delta) {
  spn_dag_run_t* run = c->handle->run;
  sp_atomic_u64_add(&run->progress.warm, (u64)delta, SP_ATOMIC_SEQ_CST);
  progress_wake(run);
}

static void diag_flush(si_dag_action_handle_t* a, spn_err_t err) {
  if (!err) {
    return;
  }
  if (a->diag.err) {
    diag_set(&a->run->diag, a->diag.err, a->diag.action, a->diag.path);
  }
  else {
    diag_set(&a->run->diag, err, a->action->id, sp_str_lit(""));
  }
}

static spn_dag_digest_t weak_key_traced(si_dag_action_handle_t* a) {
  spn_dag_digest_t key = spn_dag_weak_key(a->run->g, a->run->artifacts, a->action->id);
  trace_emit(a->run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_KEY, .action = a->action->id, .key = key });
  return key;
}

static void lookup(si_dag_action_handle_t* attempt) {
  spn_dag_run_t* run = attempt->run;
  const spn_dag_t* g = run->g;
  const spn_dag_action_t* action = attempt->action;

  switch (action->kind) {
    case SPN_DAG_ACTION_STATIC: {
      attempt->key = weak_key_traced(attempt);
      attempt->hit = try_restore(attempt, attempt->key);
      break;
    }
    case SPN_DAG_ACTION_DISCOVERED: {
      attempt->key = weak_key_traced(attempt);
      spn_dag_pathset_t set = sp_zero;
      bool present = spn_dag_obs_table_get(run->discovery, attempt->key, &set);
      trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_DISCOVERY, .action = action->id, .key = attempt->key, .hit = present });
      if (!present) {
        break;
      }
      sp_mem_arena_marker_t s = sp_mem_begin_scratch();
      u32 count = (u32)sp_da_size(set.obs);
      spn_dag_digest_t* digests = sp_alloc_n(s.mem, spn_dag_digest_t, count);
      bool resolved = !resolve_observations(run->files, set.obs, count, digests);
      spn_dag_digest_t strong = resolved ? spn_dag_strong_key(g->roots, attempt->key, set.pinned, set.obs, digests, count) : attempt->key;
      sp_mem_end_scratch(s);
      trace_resolve(run, action->id, resolved);
      if (!resolved) {
        break;
      }
      trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_STRONG, .action = action->id, .key = strong });
      attempt->hit = try_restore(attempt, strong);
      break;
    }
    case SPN_DAG_ACTION_UNCACHEABLE: {
      break;
    }
  }
}

static spn_err_t execute_attempt(sp_mem_t mem, si_dag_action_handle_t* attempt, spn_path_t staging) {
  spn_dag_run_t* run = attempt->run;
  const spn_dag_t* g = run->g;
  const spn_dag_action_t* action = attempt->action;

  attempt->outputs = sp_alloc_n(mem, spn_path_t, sp_da_size(action->produces));
  sp_da_for(action->produces, it) {
    const spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    attempt->outputs[it] = spn_path_join(mem, staging, artifact->name);
    if (artifact->kind == SPN_DAG_ARTIFACT_KIND_TREE) {
      sp_fs_create_dir_at(spn_path_at(g->roots, attempt->outputs[it]));
    }
  }

  trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_EXECUTE, .action = action->id, .key = attempt->key });

  si_dag_ctx_t c = {
    .handle = attempt,
    .consumes = action->consumes,
    .produces = action->produces,
    .roots = g->roots,
    .scratch = staging,
    .obs = action->kind == SPN_DAG_ACTION_DISCOVERED ? &attempt->obs : SP_NULLPTR,
  };
  spn_err_t err = action->execute(&c, action->user_data);
  if (err) {
    diag_set(&attempt->diag, err, action->id, sp_str_lit(""));
    return err;
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
    const spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    sp_path_t output = spn_path_at(g->roots, attempt->outputs[it]);
    err = SPN_ERR_DAG_MISSING_OUTPUT;
    if (sp_fs_exists_at(output)) {
      switch (artifact->kind) {
        case SPN_DAG_ARTIFACT_KIND_TREE: {
          err = spn_dag_store_put_tree(run->store, output, &attempt->digests[it]);
          break;
        }
        case SPN_DAG_ARTIFACT_KIND_VALUE:
        case SPN_DAG_ARTIFACT_KIND_FILE: {
          err = spn_dag_store_put_file(run->store, output, artifact->name, &attempt->digests[it]);
          break;
        }
      }
    }
    if (err) {
      diag_set(&attempt->diag, err, action->id, artifact_location(attempt, artifact));
      return err;
    }
  }

  return SPN_OK;
}

static spn_err_t execute(si_dag_action_handle_t* attempt, spn_path_t scratch) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_path_t staging = spn_path_join(s.mem, scratch, sp_fmt(s.mem, "{}", sp_fmt_uint(attempt->action->id.index)).value);
  sp_path_t dir = spn_path_at(attempt->run->g->roots, staging);
  sp_fs_remove_dir_at(dir);
  spn_err_t err = sp_fs_create_dir_at(dir) ? SPN_ERR_DAG_SCRATCH : execute_attempt(s.mem, attempt, staging);
  sp_fs_remove_dir_at(dir);
  sp_mem_end_scratch(s);
  return err;
}

static spn_err_t commit(si_dag_action_handle_t* attempt) {
  spn_dag_run_t* run = attempt->run;
  const spn_dag_t* g = run->g;
  const spn_dag_action_t* action = attempt->action;
  sp_assert(!attempt->hit);

  sp_da_for(action->produces, it) {
    artifact_state(run, action->produces[it])->digest = attempt->digests[it];
  }

  switch (action->kind) {
    case SPN_DAG_ACTION_UNCACHEABLE: {
      spn_try(settle(attempt, &attempt->diag));
      trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_COMMIT, .action = action->id });
      return SPN_OK;
    }
    case SPN_DAG_ACTION_STATIC: {
      spn_try(settle(attempt, &attempt->diag));
      record(attempt, attempt->key);
      trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_COMMIT, .action = action->id, .key = attempt->key, .hit = true });
      return SPN_OK;
    }
    case SPN_DAG_ACTION_DISCOVERED: {
      spn_dag_pathset_t set = spn_dag_obs_set_put(&attempt->obs, attempt->key);
      sp_mem_arena_marker_t s = sp_mem_begin_scratch();
      u32 count = (u32)sp_da_size(set.obs);
      spn_dag_digest_t* digests = sp_alloc_n(s.mem, spn_dag_digest_t, count);
      bool resolved = !resolve_observations(run->files, set.obs, count, digests);
      spn_dag_digest_t key = resolved ? spn_dag_strong_key(g->roots, attempt->key, set.pinned, set.obs, digests, count) : attempt->key;
      sp_mem_end_scratch(s);
      trace_resolve(run, action->id, resolved);
      if (resolved) {
        trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_STRONG, .action = action->id, .key = key });
      }
      spn_try(settle(attempt, &attempt->diag));
      if (resolved) {
        record(attempt, key);
      }
      trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_COMMIT, .action = action->id, .key = key, .hit = resolved });
      return SPN_OK;
    }
  }

  sp_unreachable_return(SPN_ERROR);
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
  spn_path_t parent = spn_path_parent(*path);
  if (spn_path_equal(parent, *path) || spn_path_empty(parent)) {
    return false;
  }
  *path = parent;
  return true;
}

static void targets_init(spn_dag_targets_t* targets, const spn_dag_t* g, sp_mem_t mem) {
  sp_ht_init(mem, targets->by_path);
  sp_ht_set_fns(targets->by_path, spn_path_on_hash, spn_path_on_compare);
  sp_ht_init(mem, targets->below);
  sp_ht_set_fns(targets->below, spn_path_on_hash, spn_path_on_compare);

  sp_da_for(g->artifacts, it) {
    const spn_dag_artifact_t* artifact = &g->artifacts[it];
    if (!artifact->producer.occupied || spn_path_empty(artifact->path)) {
      continue;
    }

    sp_assert(!spn_path_pinned(g->roots, artifact->path));
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

typedef struct loop_t loop_t;

typedef struct {
  loop_t* loop;
  const spn_dag_action_t* action;
  u64 epoch;
  spn_err_t err;
  si_dag_action_handle_t attempt;
} spn_dag_flight_t;

typedef struct {
  u32 pending;
  u32 deferred;
  bool done;
  bool parked;
  u64 done_epoch;
  sp_da(u32) waiters;
  spn_dag_flight_t flight;
} spn_dag_slot_t;

struct loop_t {
  spn_dag_run_t* run;
  spn_thread_pool_executor_t* ex;
  spn_dag_targets_t targets;
  spn_dag_slot_t* slots;
  sp_da(spn_dag_id_t) ready;
  sp_atomic_s32_t completed;
  u32 in_flight;
  spn_path_t scratch;
  spn_err_t err;
};

static void defer_producer(loop_t* loop, const spn_dag_action_t* action, u32 producer_index, u64 epoch, bool* requeue) {
  if (producer_index == action->id.index) {
    return;
  }
  spn_dag_slot_t* producer = &loop->slots[producer_index];
  if (producer->done) {
    if (producer->done_epoch > epoch && producer->flight.attempt.wrote) {
      trace_emit(loop->run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_REQUEUE, .action = action->id, .producer = loop->run->g->actions[producer_index].id });
      *requeue = true;
    }
    return;
  }
  trace_emit(loop->run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_DEFER, .action = action->id, .producer = loop->run->g->actions[producer_index].id });
  sp_da_push(producer->waiters, action->id.index);
  loop->slots[action->id.index].deferred++;
}

static bool defer_observations(loop_t* loop, const spn_dag_action_t* action, sp_da(spn_dag_obs_t) obs, u64 epoch, bool* requeue) {
  sp_da_for(obs, it) {
    const spn_dag_obs_t* o = &obs[it];
    if (spn_path_pinned(loop->run->g->roots, o->path)) {
      continue;
    }

    spn_dag_target_t* exact = sp_ht_getp(loop->targets.by_path, o->path);
    if (exact) {
      defer_producer(loop, action, exact->producer, epoch, requeue);
    }

    for (spn_path_t dir = o->path; path_parent(&dir);) {
      spn_dag_target_t* tree = sp_ht_getp(loop->targets.by_path, dir);
      if (tree && tree->kind == SPN_DAG_ARTIFACT_KIND_TREE) {
        defer_producer(loop, action, tree->producer, epoch, requeue);
      }
    }

    if (o->kind == SPN_DAG_OBS_ENUMERATION) {
      sp_da(u32)* below = sp_ht_getp(loop->targets.below, o->path);
      if (below) {
        sp_da_for(*below, bi) {
          defer_producer(loop, action, (*below)[bi], epoch, requeue);
        }
      }
    }
  }

  return loop->slots[action->id.index].deferred > 0;
}

static void seed_ready(loop_t* loop, sp_mem_t mem) {
  u64 outputs = 0;
  sp_da_for(loop->run->g->actions, ai) {
    outputs += sp_da_size(loop->run->g->actions[ai].produces);
  }
  spn_dag_digest_t* digests = sp_alloc_n(mem, spn_dag_digest_t, outputs);

  sp_da_for(loop->run->g->actions, ai) {
    const spn_dag_action_t* action = &loop->run->g->actions[ai];
    sp_da_init(mem, loop->slots[ai].waiters);
    loop->slots[ai].flight = (spn_dag_flight_t) {
      .loop = loop,
      .action = action,
      .attempt = { .digests = digests },
    };
    digests += sp_da_size(action->produces);
    sp_da_for(action->consumes, ci) {
      if (!spn_dag_digest_valid(spn_dag_run_artifact(loop->run, action->consumes[ci])->digest)) {
        loop->slots[ai].pending++;
      }
    }
    sp_da_for(action->orders, oi) {
      if (!spn_dag_digest_valid(spn_dag_run_artifact(loop->run, action->orders[oi])->digest)) {
        loop->slots[ai].pending++;
      }
    }
    if (!loop->slots[ai].pending) {
      sp_da_push(loop->ready, action->id);
    }
  }
}

static void finish_action(loop_t* loop, const spn_dag_action_t* action) {
  spn_dag_slot_t* slot = &loop->slots[action->id.index];
  slot->done = true;
  slot->done_epoch = (u64)(sp_atomic_s32_add(&loop->completed, 1, SP_ATOMIC_SEQ_CST) + 1);

  sp_da_for(action->produces, pi) {
    const spn_dag_artifact_t* produced = spn_dag_find_artifact(loop->run->g, action->produces[pi]);
    sp_da_for(produced->consumers, cj) {
      spn_dag_slot_t* consumer = &loop->slots[produced->consumers[cj].index];
      if (consumer->pending) {
        consumer->pending--;
        if (!consumer->pending) {
          sp_da_push(loop->ready, produced->consumers[cj]);
        }
      }
    }
  }

  sp_da_for(slot->waiters, wi) {
    u32 index = slot->waiters[wi];
    spn_dag_slot_t* waiter = &loop->slots[index];
    if (waiter->deferred) {
      waiter->deferred--;
      if (!waiter->deferred) {
        sp_da_push(loop->ready, loop->run->g->actions[index].id);
      }
    }
  }
}

static void flight_run(void* data) {
  spn_dag_flight_t* flight = (spn_dag_flight_t*)data;
  loop_t* loop = flight->loop;
  flight->epoch = (u64)sp_atomic_s32_load(&loop->completed, SP_ATOMIC_SEQ_CST);
  lookup(&flight->attempt);
  if (!flight->attempt.hit) {
    flight->err = execute(&flight->attempt, loop->scratch);
  }
}

static void run_commit_flight(loop_t* loop, const spn_dag_action_t* action, spn_dag_flight_t* flight) {
  loop->err = commit(&flight->attempt);
  diag_flush(&flight->attempt, loop->err);
  if (loop->err) {
    return;
  }
  progress_count(loop->run, action, false);
  finish_action(loop, action);
}

static void run_dispatch(loop_t* loop, spn_dag_id_t id) {
  const spn_dag_action_t* action = spn_dag_find_action(loop->run->g, id);
  spn_dag_slot_t* slot = &loop->slots[id.index];
  spn_dag_flight_t* flight = &slot->flight;

  if (slot->parked) {
    bool requeue = false;
    if (defer_observations(loop, action, flight->attempt.obs.rows, flight->epoch, &requeue)) {
      return;
    }
    slot->parked = false;
    if (!requeue) {
      run_commit_flight(loop, action, flight);
      return;
    }
  }

  if (action->kind == SPN_DAG_ACTION_DISCOVERED) {
    spn_dag_pathset_t set = sp_zero;
    bool requeue = false;
    if (spn_dag_obs_table_get(loop->run->discovery, spn_dag_weak_key(loop->run->g, loop->run->artifacts, action->id), &set)
      && defer_observations(loop, action, set.obs, (u64)sp_atomic_s32_load(&loop->completed, SP_ATOMIC_SEQ_CST), &requeue)) {
      return;
    }
    sp_assert(!requeue);
  }

  flight->err = SPN_OK;
  flight->attempt = (si_dag_action_handle_t) {
    .run = loop->run,
    .action = action,
    .digests = flight->attempt.digests,
    .obs.table = loop->run->discovery,
  };

  spn_thread_pool_submit(loop->ex, (spn_thread_pool_job_t) { .fn = flight_run, .data = flight });
  loop->in_flight++;
}

static void run_complete(loop_t* loop, spn_dag_flight_t* flight) {
  const spn_dag_action_t* action = flight->action;
  si_dag_action_handle_t* attempt = &flight->attempt;

  if (loop->err || flight->err) {
    diag_flush(attempt, flight->err);
    loop->err = loop->err ? loop->err : flight->err;
    return;
  }

  if (attempt->hit) {
    progress_count(loop->run, action, true);
    finish_action(loop, action);
    return;
  }

  if (action->kind == SPN_DAG_ACTION_DISCOVERED) {
    bool requeue = false;
    if (defer_observations(loop, action, attempt->obs.rows, flight->epoch, &requeue)) {
      loop->slots[action->id.index].parked = true;
      return;
    }
    if (requeue) {
      sp_da_push(loop->ready, action->id);
      return;
    }
  }

  run_commit_flight(loop, action, flight);
}

static spn_err_t run_graph(spn_dag_run_t* run, spn_thread_pool_executor_t* ex, sp_mem_t mem, spn_path_t scratch) {
  const spn_dag_t* g = run->g;
  spn_try(spn_dag_file_cache_fence_dir(run->files, scratch));

  spn_dag_violation_t violation = spn_dag_validate(g);
  if (violation.err) {
    diag_set(&run->diag, violation.err, (spn_dag_id_t) sp_zero, render_path(run, violation.path));
    return violation.err;
  }

  sp_da_for(g->artifacts, it) {
    const spn_dag_artifact_t* artifact = &g->artifacts[it];
    spn_dag_artifact_state_t* state = &run->artifacts[it];
    switch (artifact->kind) {
      case SPN_DAG_ARTIFACT_KIND_VALUE: {
        state->digest = artifact->digest;
        break;
      }
      case SPN_DAG_ARTIFACT_KIND_FILE: {
        if (artifact->producer.occupied) {
          break;
        }
        state->location = artifact->path;
        if (spn_path_pinned(g->roots, artifact->path)) {
          state->digest = spn_dag_path_digest(g->roots, artifact->path);
          break;
        }
        sp_sys_file_meta_t sys = sp_zero;
        if (spn_dag_file_cache_stat(run->files, artifact->path, &sys) || sys.kind == SP_FS_KIND_DIR || spn_dag_file_cache_digest(run->files, artifact->path, &state->digest)) {
          diag_set(&run->diag, SPN_ERR_DAG_MISSING_INPUT, (spn_dag_id_t) sp_zero, render_path(run, artifact->path));
          return SPN_ERR_DAG_MISSING_INPUT;
        }
        break;
      }
      case SPN_DAG_ARTIFACT_KIND_TREE: {
        break;
      }
    }
  }

  u64 n = sp_da_size(g->actions);
  sp_assert(n < (1u << 30));
  progress_begin(run, n);

  loop_t loop = {
    .run = run,
    .ex = ex,
    .scratch = scratch,
    .slots = sp_alloc_n(mem, spn_dag_slot_t, n ? n : 1),
    .ready = sp_da_new(mem, spn_dag_id_t),
  };
  seed_ready(&loop, mem);
  targets_init(&loop.targets, g, mem);

  u64 turns = 0;
  u64 turns_max = 4 * (n + 1) * (n + 1);
  while (true) {
    turns++;
    sp_assert(turns <= turns_max);

    if (!loop.err && run->cancel && sp_atomic_s32_load(run->cancel, SP_ATOMIC_SEQ_CST)) {
      loop.err = SPN_ERR_DAG_CANCELLED;
    }

    spn_thread_pool_job_t job = spn_thread_pool_try_poll(ex);
    if (job.fn) {
      loop.in_flight--;
      run_complete(&loop, (spn_dag_flight_t*)job.data);
      continue;
    }
    if (!loop.err && !sp_da_empty(loop.ready)) {
      spn_dag_id_t id = *sp_da_back(loop.ready);
      sp_da_pop(loop.ready);
      run_dispatch(&loop, id);
      continue;
    }
    if (loop.in_flight) {
      job = spn_thread_pool_poll(ex);
      loop.in_flight--;
      run_complete(&loop, (spn_dag_flight_t*)job.data);
      continue;
    }
    break;
  }

  if (!loop.err && (u64)sp_atomic_s32_load(&loop.completed, SP_ATOMIC_SEQ_CST) != n) {
    return SPN_ERR_DAG_STALLED;
  }
  return loop.err;
}

spn_err_t spn_dag_run(spn_dag_run_t* run, spn_thread_pool_executor_t* ex) {
  const spn_dag_t* g = run->g;
  sp_assert(run->discovery);
  sp_assert(!spn_path_empty(run->scratch));
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();

  u64 artifacts = sp_da_size(g->artifacts);
  run->artifacts = sp_alloc_n(run->mem, spn_dag_artifact_state_t, artifacts ? artifacts : 1);
  run->diag = (spn_dag_diag_t) sp_zero;

  spn_err_t err = SPN_ERR_DAG_SCRATCH;
  spn_path_t scratch = sp_zero;
  spn_path_t base = spn_path_join(s.mem, run->scratch, sp_str_lit("scratch/run"));
  if (!spn_path_stage_dir(s.mem, g->roots, base, sp_str_lit("tmp"), &scratch)) {
    err = run_graph(run, ex, s.mem, scratch);
    sp_fs_remove_dir_at(spn_path_at(g->roots, scratch));
  }
  if (err) {
    diag_set(&run->diag, err, (spn_dag_id_t) sp_zero, sp_str_lit(""));
  }

  sp_mem_end_scratch(s);
  return err;
}
