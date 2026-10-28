#include "dag/dag.h"
#include "dag/run.h"
#include "hash_table/hash_table.h"
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
  if (meta.id.device && meta.id.device != sys.device) {
    return false;
  }
  if (meta.id.inode != sys.id) {
    return false;
  }
  if (!is_timespec_equal(meta.mtime, sys.mtime)) {
    return false;
  }
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

static spn_path_t file_cache_canonical(spn_dag_file_cache_t* c, spn_path_t path) {
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

static void trace_emit(const spn_dag_run_t* run, spn_dag_trace_event_t event) {
  if (run->trace) {
    run->trace(&event, run->trace_data);
  }
}

static void progress_wake(spn_dag_run_t* run) {
  if (run->wake) {
    spn_wake_ring(run->wake);
  }
}

static spn_dag_diag_t diag_at(si_dag_action_handle_t* a, spn_err_t err, const spn_dag_artifact_t* artifact, spn_path_t path) {
  spn_dag_run_t* run = a->run;
  sp_mutex_lock(&run->mutex);
  spn_path_t copy = spn_path_copy(run->mem, path);
  sp_mutex_unlock(&run->mutex);
  return (spn_dag_diag_t) {
    .err = err,
    .action = a->action->id,
    .artifact = artifact->id,
    .path = copy,
  };
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

// @spader @cleanup
static spn_dag_diag_t settle_tree(si_dag_action_handle_t* a, const spn_dag_artifact_t* artifact) {
  spn_dag_run_t* run = a->run;
  const spn_dag_t* g = run->g;
  const spn_dag_action_t* action = a->action;
  spn_dag_digest_t digest = spn_dag_run_artifact(run, artifact->id)->digest;
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_dag_diag_t diag = sp_zero;

  sp_da(spn_dag_action_output_t) entries = sp_zero;
  spn_err_t err = spn_dag_tree_entries(run->store, digest, s.mem, &entries);
  if (err) {
    diag = diag_at(a, err, artifact, artifact->path);
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
      diag = diag_at(a, SPN_ERR_DAG_STORE_READ, artifact, artifact->path);
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
      diag = diag_at(a, SPN_ERR_DAG_STORE_WRITE, artifact, artifact->path);
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
    diag = diag_at(a, SPN_ERR_DAG_STORE_MISSING, artifact, paths[poisoned[0]]);
    goto done;
  }

  sp_da_rfor(extra, it) {
    spn_path_t path = spn_path_join(s.mem, artifact->path, extra[it].rel);
    sp_path_t at = spn_path_at(g->roots, path);
    sp_err_t removed = extra[it].kind == SP_FS_KIND_DIR ? sp_fs_remove_dir_at(at) : sp_fs_remove_file_at(at);
    if (removed) {
      diag = diag_at(a, SPN_ERR_DAG_STORE_WRITE, artifact, path);
      goto done;
    }
    spn_dag_file_cache_invalidate(run->files, path);
  }

  sp_da_for(stale, it) {
    spn_dag_action_output_t* entry = &entries[stale[it]];
    err = link_target(run, paths[stale[it]], entry->name, entry->digest);
    if (err) {
      diag = diag_at(a, err, artifact, paths[stale[it]]);
      goto done;
    }
  }
  trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = digest, .hit = false });

done:
  sp_mem_end_scratch(s);
  return diag;
}

static spn_dag_diag_t settle(si_dag_action_handle_t* a) {
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
        return diag_at(a, located, artifact, sp_zero_s(spn_path_t));
      }
      continue;
    }
    switch (artifact->kind) {
      case SPN_DAG_ARTIFACT_KIND_TREE: {
        spn_dag_diag_t diag = settle_tree(a, artifact);
        if (diag.err) {
          return diag;
        }
        break;
      }
      case SPN_DAG_ARTIFACT_KIND_VALUE:
      case SPN_DAG_ARTIFACT_KIND_FILE: {
        switch (target_state(run, artifact->path, artifact->name, produced->digest)) {
          case TARGET_SETTLED: {
            trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = produced->digest, .hit = true });
            break;
          }
          case TARGET_POISONED: {
            spn_dag_store_drop(run->store, produced->digest, artifact->name);
            return diag_at(a, SPN_ERR_DAG_STORE_MISSING, artifact, artifact->path);
          }
          case TARGET_STALE: {
            a->wrote = true;
            spn_err_t err = link_target(run, artifact->path, artifact->name, produced->digest);
            if (err) {
              return diag_at(a, err, artifact, artifact->path);
            }
            trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_SETTLE, .action = action->id, .producer = artifact->id, .key = produced->digest, .hit = false });
            break;
          }
        }
        break;
      }
    }
    produced->location = artifact->path;
  }
  return (spn_dag_diag_t) sp_zero;
}

static bool try_restore(si_dag_action_handle_t* a, spn_dag_digest_t key) {
  spn_dag_run_t* run = a->run;
  const spn_dag_action_t* action = a->action;
  spn_dag_action_entry_t entry = sp_zero;
  bool present = spn_dag_action_cache_get(run->cache, key, &entry);
  bool hit = present && sp_da_size(entry.outputs) == sp_da_size(action->produces);
  if (hit) {
    sp_da_for(entry.outputs, it) {
      if (!sp_str_equal(entry.outputs[it].name, spn_dag_find_artifact(run->g, action->produces[it])->name)) {
        hit = false;
        break;
      }
    }
  }
  if (hit) {
    sp_da_for(entry.outputs, it) {
      artifact_state(run, action->produces[it])->digest = entry.outputs[it].digest;
    }
    hit = !settle(a).err;
  }
  trace_emit(run, (spn_dag_trace_event_t) {
    .kind = SPN_DAG_TRACE_CACHE,
    .action = action->id,
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

static spn_err_t resolve_one(spn_dag_file_cache_t* files, const spn_dag_obs_t* o, spn_dag_digest_t* digest) {
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

static bool strong_key(si_dag_action_handle_t* a, const spn_dag_pathset_t* set, spn_dag_digest_t* strong) {
  spn_dag_run_t* run = a->run;
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  u32 count = (u32)sp_da_size(set->obs);
  spn_dag_digest_t* digests = sp_alloc_n(s.mem, spn_dag_digest_t, count);
  bool resolved = true;
  sp_for(it, count) {
    if (resolve_one(run->files, &set->obs[it], &digests[it])) {
      resolved = false;
      break;
    }
  }
  if (resolved) {
    *strong = spn_dag_strong_key(run->g->roots, a->key, set->pinned, set->obs, digests, count);
  }
  sp_mem_end_scratch(s);

  trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_RESOLVE, .action = a->action->id, .hit = resolved });
  if (resolved) {
    trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_STRONG, .action = a->action->id, .key = *strong });
  }
  return resolved;
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
  return file_cache_canonical(c->handle->run->files, path);
}

void si_dag_observe(const si_dag_ctx_t* c, spn_dag_obs_t obs) {
  spn_dag_observe(c->obs, obs);
}

void si_dag_add_warm(const si_dag_ctx_t* c, s64 delta) {
  spn_dag_run_t* run = c->handle->run;
  sp_atomic_u64_add(&run->progress.warm, (u64)delta, SP_ATOMIC_SEQ_CST);
  progress_wake(run);
}

static spn_dag_digest_t weak_key_traced(si_dag_action_handle_t* a) {
  spn_dag_digest_t key = spn_dag_weak_key(a->run->g, a->run->artifacts, a->action->id);
  trace_emit(a->run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_KEY, .action = a->action->id, .key = key });
  return key;
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

  spn_dag_obs_set_t* obs = action->kind == SPN_DAG_ACTION_DISCOVERED ? &attempt->obs : SP_NULLPTR;
  si_dag_ctx_t c = {
    .handle = attempt,
    .consumes = action->consumes,
    .produces = action->produces,
    .roots = g->roots,
    .scratch = staging,
    .obs = obs,
  };
  spn_err_t err = action->execute(&c, action->user_data);
  if (err) {
    attempt->diag = (spn_dag_diag_t) { .err = err, .action = action->id };
    return err;
  }
  if (obs) {
    spn_dag_obs_canonicalize(obs->rows);
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
      attempt->diag = diag_at(attempt, err, artifact, artifact->path);
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

typedef struct {
  u32 producer;
  spn_dag_artifact_kind_t kind;
} target_t;

typedef struct {
  u32 pending;
  u32 deferred;
  u64 done_epoch;
  bool done;
  bool wrote;
  si_da(u32) waiters;
} slot_t;

typedef struct {
  const spn_dag_run_t* run;
  slot_t* slots;
  si_ht(spn_path_t, target_t) by_path;
  si_ht(spn_path_t, si_da(u32)) below;
  si_da(spn_dag_id_t) ready;
  sp_atomic_s32_t completed;
} sched_t;

typedef enum {
  SCHED_PROCEED,
  SCHED_WAIT,
  SCHED_REQUEUE,
} sched_verdict_t;

static bool path_parent(spn_path_t* path) {
  spn_path_t parent = spn_path_parent(*path);
  if (spn_path_equal(parent, *path) || spn_path_empty(parent)) {
    return false;
  }
  *path = parent;
  return true;
}

static void sched_init(sched_t* s, sp_mem_t mem, const spn_dag_run_t* run) {
  const spn_dag_t* g = run->g;
  u64 n = sp_da_size(g->actions);
  *s = (sched_t) {
    .run = run,
    .slots = sp_alloc_n(mem, slot_t, n ? n : 1),
  };
  si_ht_init_ex(mem, s->by_path, spn_path_on_hash, spn_path_on_compare);
  si_ht_init_ex(mem, s->below, spn_path_on_hash, spn_path_on_compare);

  sp_da_for(g->artifacts, it) {
    const spn_dag_artifact_t* artifact = &g->artifacts[it];
    if (!artifact->producer.occupied || spn_path_empty(artifact->path)) {
      continue;
    }

    si_ht_insert(mem, s->by_path, artifact->path, ((target_t) {
      .producer = artifact->producer.index,
      .kind = artifact->kind
    }));

    for (spn_path_t dir = artifact->path; path_parent(&dir);) {
      si_da(u32)* below = si_ht_getp(s->below, dir);
      if (!below) {
        si_ht_insert(mem, s->below, dir, SP_NULLPTR);
        below = si_ht_getp(s->below, dir);
      }
      si_da_push(mem, *below, artifact->producer.index);
    }
  }

  sp_da_for(g->actions, ai) {
    const spn_dag_action_t* action = &g->actions[ai];
    slot_t* slot = &s->slots[ai];
    sp_da_for(action->consumes, ci) {
      if (!spn_dag_digest_valid(spn_dag_run_artifact(run, action->consumes[ci])->digest)) {
        slot->pending++;
      }
    }
    sp_da_for(action->orders, oi) {
      if (!spn_dag_digest_valid(spn_dag_run_artifact(run, action->orders[oi])->digest)) {
        slot->pending++;
      }
    }
    if (!slot->pending) {
      si_da_push(mem, s->ready, action->id);
    }
  }
}

static u64 sched_epoch(sched_t* s) {
  return (u64)sp_atomic_s32_load(&s->completed, SP_ATOMIC_SEQ_CST);
}

static bool sched_defer_producer(sched_t* s, sp_mem_t mem, spn_dag_id_t action, u32 producer_index, u64 epoch) {
  if (producer_index == action.index) {
    return false;
  }
  slot_t* producer = &s->slots[producer_index];
  spn_dag_id_t producer_id = s->run->g->actions[producer_index].id;
  if (producer->done) {
    bool stale = producer->done_epoch > epoch && producer->wrote;
    if (stale) {
      trace_emit(s->run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_REQUEUE, .action = action, .producer = producer_id });
    }
    return stale;
  }
  trace_emit(s->run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_DEFER, .action = action, .producer = producer_id });
  si_da_push(mem, producer->waiters, action.index);
  s->slots[action.index].deferred++;
  return false;
}

static sched_verdict_t sched_defer(sched_t* s, sp_mem_t mem, spn_dag_id_t action, sp_da(spn_dag_obs_t) obs, u64 epoch) {
  const spn_path_roots_t* roots = s->run->g->roots;
  bool requeue = false;
  sp_da_for(obs, it) {
    const spn_dag_obs_t* o = &obs[it];
    if (spn_path_pinned(roots, o->path)) {
      continue;
    }

    target_t* exact = si_ht_getp(s->by_path, o->path);
    if (exact && sched_defer_producer(s, mem, action, exact->producer, epoch)) {
      requeue = true;
    }

    for (spn_path_t dir = o->path; path_parent(&dir);) {
      target_t* tree = si_ht_getp(s->by_path, dir);
      if (tree && tree->kind == SPN_DAG_ARTIFACT_KIND_TREE && sched_defer_producer(s, mem, action, tree->producer, epoch)) {
        requeue = true;
      }
    }

    if (o->kind == SPN_DAG_OBS_ENUMERATION) {
      si_da(u32)* below = si_ht_getp(s->below, o->path);
      if (below) {
        si_da_for(*below, bi) {
          if (sched_defer_producer(s, mem, action, (*below)[bi], epoch)) {
            requeue = true;
          }
        }
      }
    }
  }

  if (s->slots[action.index].deferred) {
    return SCHED_WAIT;
  }
  return requeue ? SCHED_REQUEUE : SCHED_PROCEED;
}

static void sched_finish(sched_t* s, sp_mem_t mem, spn_dag_id_t action, bool wrote) {
  const spn_dag_t* g = s->run->g;
  slot_t* slot = &s->slots[action.index];
  slot->done = true;
  slot->wrote = wrote;
  slot->done_epoch = (u64)(sp_atomic_s32_add(&s->completed, 1, SP_ATOMIC_SEQ_CST) + 1);

  const spn_dag_action_t* finished = spn_dag_find_action(g, action);
  sp_da_for(finished->produces, pi) {
    const spn_dag_artifact_t* produced = spn_dag_find_artifact(g, finished->produces[pi]);
    sp_da_for(produced->consumers, cj) {
      slot_t* consumer = &s->slots[produced->consumers[cj].index];
      if (consumer->pending) {
        consumer->pending--;
        if (!consumer->pending) {
          si_da_push(mem, s->ready, produced->consumers[cj]);
        }
      }
    }
  }

  si_da_for(slot->waiters, wi) {
    u32 index = slot->waiters[wi];
    slot_t* waiter = &s->slots[index];
    if (waiter->deferred) {
      waiter->deferred--;
      if (!waiter->deferred) {
        si_da_push(mem, s->ready, g->actions[index].id);
      }
    }
  }
}

typedef struct {
  u64 epoch;
  spn_err_t err;
  bool parked;
  si_dag_action_handle_t attempt;
} flight_t;

struct si_dag_run_state {
  sp_mem_t mem;
  sched_t sched;
  flight_t* flights;
  spn_path_t scratch;
  u64 turns;
};

static flight_t* flight_at(spn_dag_run_t* run, spn_dag_id_t id) {
  sp_assert(id.occupied);
  sp_assert(id.graph == run->g->id);
  return &run->state->flights[id.index];
}

static spn_err_t begin(spn_dag_run_t* run, sp_mem_t mem, spn_path_t scratch) {
  const spn_dag_t* g = run->g;
  spn_err_t err = spn_dag_file_cache_fence_dir(run->files, scratch);
  if (err) {
    run->diag = (spn_dag_diag_t) { .err = err, .path = scratch };
    return err;
  }

  sp_da_for(g->artifacts, it) {
    const spn_dag_artifact_t* artifact = &g->artifacts[it];
    switch (artifact->kind) {
      case SPN_DAG_ARTIFACT_KIND_VALUE: {
        run->artifacts[it].digest = artifact->digest;
        break;
      }
      case SPN_DAG_ARTIFACT_KIND_FILE: {
        if (artifact->producer.occupied) {
          break;
        }
        run->artifacts[it].location = artifact->path;
        if (spn_path_pinned(g->roots, artifact->path)) {
          run->artifacts[it].digest = spn_dag_path_digest(g->roots, artifact->path);
          break;
        }
        sp_sys_file_meta_t sys = sp_zero;
        if (spn_dag_file_cache_stat(run->files, artifact->path, &sys) || sys.kind == SP_FS_KIND_DIR || spn_dag_file_cache_digest(run->files, artifact->path, &run->artifacts[it].digest)) {
          run->diag = (spn_dag_diag_t) { .err = SPN_ERR_DAG_MISSING_INPUT, .artifact = artifact->id, .path = artifact->path };
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
  sp_atomic_s32_store(&run->progress.total, (s32)n, SP_ATOMIC_SEQ_CST);
  sp_atomic_s32_store(&run->progress.completed, 0, SP_ATOMIC_SEQ_CST);
  sp_atomic_s32_store(&run->progress.hits, 0, SP_ATOMIC_SEQ_CST);
  sp_atomic_s32_store(&run->progress.misses, 0, SP_ATOMIC_SEQ_CST);
  sp_atomic_u64_store(&run->progress.warm, 0, SP_ATOMIC_SEQ_CST);
  progress_wake(run);

  si_dag_run_state_t* state = sp_alloc_n(mem, si_dag_run_state_t, 1);
  *state = (si_dag_run_state_t) {
    .mem = mem,
    .flights = sp_alloc_n(mem, flight_t, n ? n : 1),
    .scratch = scratch,
  };
  sched_init(&state->sched, mem, run);

  u64 outputs = 0;
  sp_da_for(g->actions, ai) {
    outputs += sp_da_size(g->actions[ai].produces);
  }
  spn_dag_digest_t* digests = sp_alloc_n(mem, spn_dag_digest_t, outputs);
  sp_da_for(g->actions, ai) {
    state->flights[ai].attempt = (si_dag_action_handle_t) {
      .action = &g->actions[ai],
      .digests = digests,
    };
    digests += sp_da_size(g->actions[ai].produces);
  }

  run->state = state;
  return SPN_OK;
}

spn_err_t spn_dag_run_begin(spn_dag_run_t* run, sp_mem_t mem) {
  const spn_dag_t* g = run->g;
  sp_assert(run->discovery);
  sp_assert(!spn_path_empty(run->scratch));
  sp_assert(sp_da_size(g->actions) < (1u << 30));

  run->diag = spn_dag_validate(g);
  if (run->diag.err) {
    return run->diag.err;
  }

  u64 artifacts = sp_da_size(g->artifacts);
  run->artifacts = sp_alloc_n(run->mem, spn_dag_artifact_state_t, artifacts ? artifacts : 1);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_path_t base = spn_path_join(s.mem, run->scratch, sp_str_lit("scratch/run"));
  spn_path_t scratch = sp_zero;
  spn_err_t err = spn_path_stage_dir(run->mem, g->roots, base, sp_str_lit("tmp"), &scratch);
  sp_mem_end_scratch(s);
  if (err) {
    run->diag = (spn_dag_diag_t) { .err = SPN_ERR_DAG_SCRATCH };
    return SPN_ERR_DAG_SCRATCH;
  }

  err = begin(run, mem, scratch);
  if (err) {
    sp_fs_remove_dir_at(spn_path_at(g->roots, scratch));
  }
  return err;
}

spn_dag_id_t spn_dag_run_next(spn_dag_run_t* run) {
  si_dag_run_state_t* state = run->state;
  sched_t* sched = &state->sched;
  u64 n = sp_da_size(run->g->actions);

  state->turns++;
  sp_assert(state->turns <= 4 * (n + 1) * (n + 1));

  if (!run->diag.err && run->cancel && sp_atomic_s32_load(run->cancel, SP_ATOMIC_SEQ_CST)) {
    run->diag = (spn_dag_diag_t) { .err = SPN_ERR_DAG_CANCELLED };
  }

  while (!run->diag.err && !si_da_empty(sched->ready)) {
    spn_dag_id_t id = *si_da_back(sched->ready);
    si_da_pop(sched->ready);
    flight_t* flight = flight_at(run, id);
    if (flight->parked) {
      spn_dag_run_complete(run, id);
      continue;
    }

    si_dag_action_handle_t* attempt = &flight->attempt;
    const spn_dag_action_t* action = attempt->action;
    flight->err = SPN_OK;
    *attempt = (si_dag_action_handle_t) {
      .run = run,
      .action = action,
      .digests = attempt->digests,
      .obs.table = run->discovery,
    };

    switch (action->kind) {
      case SPN_DAG_ACTION_STATIC: {
        attempt->key = weak_key_traced(attempt);
        break;
      }
      case SPN_DAG_ACTION_DISCOVERED: {
        attempt->key = weak_key_traced(attempt);
        spn_dag_pathset_t set = sp_zero;
        if (spn_dag_obs_table_get(run->discovery, attempt->key, &set)) {
          sched_verdict_t verdict = sched_defer(sched, state->mem, action->id, set.obs, sched_epoch(sched));
          sp_assert(verdict != SCHED_REQUEUE);
          if (verdict == SCHED_WAIT) {
            continue;
          }
        }
        break;
      }
      case SPN_DAG_ACTION_UNCACHEABLE: {
        break;
      }
    }
    return id;
  }

  return (spn_dag_id_t) sp_zero;
}

void spn_dag_run_attempt(spn_dag_run_t* run, spn_dag_id_t id) {
  si_dag_run_state_t* state = run->state;
  flight_t* flight = flight_at(run, id);
  si_dag_action_handle_t* attempt = &flight->attempt;
  flight->epoch = sched_epoch(&state->sched);

  switch (attempt->action->kind) {
    case SPN_DAG_ACTION_STATIC: {
      attempt->hit = try_restore(attempt, attempt->key);
      break;
    }
    case SPN_DAG_ACTION_DISCOVERED: {
      spn_dag_pathset_t set = sp_zero;
      bool present = spn_dag_obs_table_get(run->discovery, attempt->key, &set);
      trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_DISCOVERY, .action = id, .key = attempt->key, .hit = present });
      spn_dag_digest_t strong = sp_zero;
      if (present && strong_key(attempt, &set, &strong)) {
        attempt->hit = try_restore(attempt, strong);
      }
      break;
    }
    case SPN_DAG_ACTION_UNCACHEABLE: {
      break;
    }
  }

  if (!attempt->hit) {
    flight->err = execute(attempt, state->scratch);
  }
}

void spn_dag_run_complete(spn_dag_run_t* run, spn_dag_id_t id) {
  si_dag_run_state_t* state = run->state;
  sched_t* sched = &state->sched;
  flight_t* flight = flight_at(run, id);
  si_dag_action_handle_t* attempt = &flight->attempt;
  const spn_dag_action_t* action = attempt->action;
  flight->parked = false;

  if (run->diag.err) {
    return;
  }

  spn_err_t err = flight->err;
  if (!err && !attempt->hit) {
    if (action->kind == SPN_DAG_ACTION_DISCOVERED) {
      switch (sched_defer(sched, state->mem, action->id, attempt->obs.rows, flight->epoch)) {
        case SCHED_WAIT: {
          flight->parked = true;
          return;
        }
        case SCHED_REQUEUE: {
          si_da_push(state->mem, sched->ready, action->id);
          return;
        }
        case SCHED_PROCEED: {
          break;
        }
      }
    }

    sp_da_for(action->produces, it) {
      artifact_state(run, action->produces[it])->digest = attempt->digests[it];
    }
    switch (action->kind) {
      case SPN_DAG_ACTION_UNCACHEABLE: {
        attempt->diag = settle(attempt);
        err = attempt->diag.err;
        if (!err) {
          trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_COMMIT, .action = action->id });
        }
        break;
      }
      case SPN_DAG_ACTION_STATIC: {
        attempt->diag = settle(attempt);
        err = attempt->diag.err;
        if (!err) {
          record(attempt, attempt->key);
          trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_COMMIT, .action = action->id, .key = attempt->key, .hit = true });
          sp_atomic_s32_add(&run->progress.misses, 1, SP_ATOMIC_SEQ_CST);
        }
        break;
      }
      case SPN_DAG_ACTION_DISCOVERED: {
        spn_dag_pathset_t set = spn_dag_obs_set_put(&attempt->obs, attempt->key);
        spn_dag_digest_t key = attempt->key;
        bool resolved = strong_key(attempt, &set, &key);
        attempt->diag = settle(attempt);
        err = attempt->diag.err;
        if (!err) {
          if (resolved) {
            record(attempt, key);
          }
          trace_emit(run, (spn_dag_trace_event_t) { .kind = SPN_DAG_TRACE_COMMIT, .action = action->id, .key = key, .hit = resolved });
          sp_atomic_s32_add(&run->progress.misses, 1, SP_ATOMIC_SEQ_CST);
        }
        break;
      }
    }
  }

  if (err) {
    run->diag = attempt->diag.err ? attempt->diag : (spn_dag_diag_t) { .err = err, .action = action->id };
    return;
  }

  if (attempt->hit) {
    sp_atomic_s32_add(&run->progress.hits, 1, SP_ATOMIC_SEQ_CST);
  }
  sp_atomic_s32_add(&run->progress.completed, 1, SP_ATOMIC_SEQ_CST);
  progress_wake(run);
  sched_finish(sched, state->mem, action->id, attempt->wrote);
}

spn_err_t spn_dag_run_end(spn_dag_run_t* run) {
  si_dag_run_state_t* state = run->state;
  if (!run->diag.err && sched_epoch(&state->sched) != sp_da_size(run->g->actions)) {
    run->diag = (spn_dag_diag_t) { .err = SPN_ERR_DAG_STALLED };
  }
  sp_fs_remove_dir_at(spn_path_at(run->g->roots, state->scratch));
  run->state = SP_NULLPTR;
  return run->diag.err;
}

static void flight_run(void* data) {
  flight_t* flight = (flight_t*)data;
  spn_dag_run_attempt(flight->attempt.run, flight->attempt.action->id);
}

spn_err_t spn_dag_run(spn_dag_run_t* run, spn_thread_pool_t* pool) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = spn_dag_run_begin(run, s.mem);
  if (err) {
    sp_mem_end_scratch(s);
    return err;
  }

  u32 in_flight = 0;
  while (true) {
    spn_thread_pool_job_t job = spn_thread_pool_try_poll(pool);
    if (job.fn) {
      in_flight--;
      spn_dag_run_complete(run, ((flight_t*)job.data)->attempt.action->id);
      continue;
    }
    spn_dag_id_t id = spn_dag_run_next(run);
    if (id.occupied) {
      in_flight++;
      spn_thread_pool_submit(pool, (spn_thread_pool_job_t) { .fn = flight_run, .data = flight_at(run, id) });
      continue;
    }
    if (!in_flight) {
      break;
    }
    job = spn_thread_pool_poll(pool);
    in_flight--;
    spn_dag_run_complete(run, ((flight_t*)job.data)->attempt.action->id);
  }

  err = spn_dag_run_end(run);
  sp_mem_end_scratch(s);
  return err;
}
