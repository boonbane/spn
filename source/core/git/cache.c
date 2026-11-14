#include "sp.h"
#include "macro/macro.h"
#include "git/cache.h"

#include "external/git.h"
#include "git/remote.h"
#include "git/key.h"
#include "fs/fs.h"
#include "paths/paths.h"
#include "str/str.h"

void spn_git_cache_init(spn_git_cache_t* cache, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_path_t db, spn_path_t checkouts) {
  *cache = (spn_git_cache_t) {
    .mem = mem,
    .intern = intern,
    .roots = roots,
    .db.dir = spn_path_copy(mem, db),
    .checkouts.dir = spn_path_copy(mem, checkouts),
  };

  sp_str_ht_init(mem, cache->db.entries);
  sp_str_om_init(cache->checkouts.entries);

  sp_fs_create_dir_at(spn_path_at(roots, cache->db.dir));
}

static spn_git_db_t* spn_git_cache_db_entry(spn_git_cache_t* cache, sp_str_t url) {
  sp_mutex_lock(&cache->mutex);

  sp_str_t key = spn_git_db_key(cache->mem, url);
  spn_git_db_t** existing = sp_str_ht_get(cache->db.entries, key);
  spn_git_db_t* db = existing ? *existing : SP_NULLPTR;
  if (!db) {
    db = sp_alloc_type(cache->mem, spn_git_db_t);
    db->url = url;
    db->path = spn_path_join(cache->mem, cache->db.dir, key);
    sp_str_ht_insert(cache->db.entries, key, db);
  }

  sp_mutex_unlock(&cache->mutex);
  return db;
}

spn_err_t spn_git_cache_ensure_db(spn_git_cache_t* cache, sp_str_t url, sp_atomic_s32_t* cancelled, spn_git_db_t** db) {
  spn_git_db_t* entry = spn_git_cache_db_entry(cache, url);
  *db = entry;

  sp_mutex_lock(&entry->mutex);
  if (!entry->ready) {
    entry->ready = true;

    sp_path_t repo = spn_path_at(cache->roots, entry->path);
    if (!sp_fs_is_dir_at(repo)) {
      sp_fs_lock_t lock = sp_zero;
      entry->err = spn_git_lock(repo, repo, url, cancelled, &lock, &entry->failure);
      if (!entry->err && !sp_fs_is_dir_at(repo)) {
        sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
        entry->err = spn_git_init_bare(url, spn_path_str(cache->roots, scratch.mem, entry->path), &entry->failure);
        sp_mem_end_scratch(scratch);
      }
      sp_fs_lock_release(&lock);
    }
  }
  sp_mutex_unlock(&entry->mutex);

  return entry->err;
}

static void fail(spn_git_failure_t* failure, sp_str_t message) {
  *failure = sp_zero_s(spn_git_failure_t);
  sp_str_copy_to(message, failure->message, spn_git_message_max - 1);
}

spn_err_t spn_git_db_ensure_rev(spn_git_cache_t* cache, spn_git_db_t* db, sp_str_t rev, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t path = spn_path_str(cache->roots, scratch.mem, db->path);

  bool found = spn_git_has_rev(path, rev);
  if (!found) {
    spn_err_t fetched = spn_git_fetch_rev(cache->transport, path, db->url, rev, cache->backoff, cancelled, failure);

    spn_path_t staged = sp_zero;
    if (fetched && failure->kind == SPN_GIT_CORRUPT && !spn_path_stage_dir(scratch.mem, cache->roots, db->path, sp_str_lit("tmp"), &staged)) {
      sp_path_t live = spn_path_at(cache->roots, db->path);
      sp_path_t fresh = spn_path_at(cache->roots, staged);
      sp_path_t retired = spn_path_at(cache->roots, spn_path_staging(scratch.mem, db->path, sp_str_lit("tmp")));
      sp_str_t repo = spn_path_str(cache->roots, scratch.mem, staged);

      spn_git_failure_t rebuilt = sp_zero;
      if (spn_git_init_bare(db->url, repo, &rebuilt) || spn_git_fetch_rev(cache->transport, repo, db->url, rev, cache->backoff, cancelled, &rebuilt)) {
        *failure = rebuilt;
      }
      else if (!sp_fs_replace_dir_at(fresh, live, retired)) {
        fetched = SPN_OK;
      }
      sp_fs_remove_dir_at(fresh);
    }

    found = spn_git_has_rev(path, rev);
    if (!fetched && !found) {
      fail(failure, sp_fmt(scratch.mem, "{} has no rev {}", sp_fmt_str(db->url), sp_fmt_str(rev)).value);
    }
  }

  sp_mem_end_scratch(scratch);
  return found ? SPN_OK : SPN_ERROR;
}

static void fail_at(spn_git_cache_t* cache, spn_git_checkout_t* entry, const c8* what) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t path = spn_path_str(cache->roots, scratch.mem, entry->path);
  fail(&entry->failure, sp_fmt(scratch.mem, "failed to {} checkout at {}", sp_fmt_cstr(what), sp_fmt_str(path)).value);
  sp_mem_end_scratch(scratch);
}

static spn_err_t spn_git_cache_fill_checkout(spn_git_cache_t* cache, spn_git_checkout_t* entry, spn_git_db_t* db, spn_path_t staged) {
  sp_str_buf_t buf = sp_zero;
  sp_str_t work = spn_path_str(cache->roots, sp_str_buf_as_mem(&buf), staged);
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t error = sp_zero;
  spn_err_t err = spn_git_clone_shared(scratch.mem, spn_path_str(cache->roots, scratch.mem, db->path), work, &error);
  if (err) {
    fail(&entry->failure, error);
  }
  else if (spn_git_checkout(work, entry->id.rev)) {
    fail(&entry->failure, sp_fmt(scratch.mem, "failed to check out {}", sp_fmt_str(entry->id.rev)).value);
    err = SPN_ERROR;
  }
  sp_mem_end_scratch(scratch);
  spn_try(err);

  si_da_for_value(entry->id.patches.files, file) {
    sp_mem_arena_marker_t s = sp_mem_begin_scratch();
    sp_str_t rejected = sp_zero;
    spn_err_t applied = spn_git_apply(s.mem, work, file, &rejected);
    if (applied) {
      fail(&entry->failure, sp_fmt(s.mem, "failed to apply {}: {}", sp_fmt_str(file), sp_fmt_str(rejected)).value);
    }
    sp_mem_end_scratch(s);
    if (applied) {
      return applied;
    }
  }

  return SPN_OK;
}

static spn_err_t spn_git_cache_materialize_checkout(spn_git_cache_t* cache, spn_git_checkout_t* entry, sp_atomic_s32_t* cancelled) {
  sp_path_t dest = spn_path_at(cache->roots, entry->path);
  if (entry->fetched) {
    spn_git_db_t* db = SP_NULLPTR;
    if (spn_git_cache_ensure_db(cache, entry->id.url, cancelled, &db)) {
      entry->failure = db->failure;
      return SPN_ERROR;
    }

    // libgit2 caps at 168 characters for the path of a repo, which of course
    // is completely unconfigurable short of patching. Which I did, but then
    // it turns out that cl.exe chokes on paths not much larger. So instead,
    // write some stupid fucking code to shorten the path.
    spn_path_t staged = sp_zero;
    if (spn_path_stage_dir(cache->mem, cache->roots, spn_path_join(cache->mem, cache->checkouts.dir, sp_str_lit("t")), sp_str_lit("tmp"), &staged)) {
      fail_at(cache, entry, "stage");
      return SPN_ERROR;
    }

    sp_path_t work = spn_path_at(cache->roots, staged);
    sp_path_t repo = spn_path_at(cache->roots, db->path);
    sp_fs_lock_t lock = sp_zero;
    sp_mutex_lock(&db->mutex);
    bool filled =
      !spn_git_lock(repo, repo, db->url, cancelled, &lock, &entry->failure) &&
      !spn_git_db_ensure_rev(cache, db, entry->id.rev, cancelled, &entry->failure) &&
      !spn_git_cache_fill_checkout(cache, entry, db, staged);
    sp_fs_lock_release(&lock);
    sp_mutex_unlock(&db->mutex);
    if (!filled) {
      sp_fs_remove_dir_at(work);
      return SPN_ERROR;
    }

    if (sp_sys_rename_s(work.dir, work.sub, dest.dir, dest.sub)) {
      sp_fs_remove_dir_at(work);
      if (!sp_fs_is_dir_at(dest)) {
        fail_at(cache, entry, "place");
        return SPN_ERROR;
      }
    }
  }

  if (!sp_str_empty(entry->id.dir)) {
    spn_path_t subdir = spn_path_join(cache->mem, entry->path, entry->id.dir);
    if (!sp_fs_is_dir_at(spn_path_at(cache->roots, subdir))) {
      sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
      fail(&entry->failure, sp_fmt(scratch.mem, "{} does not exist in {}", sp_fmt_str(entry->id.dir), sp_fmt_str(entry->id.url)).value);
      sp_mem_end_scratch(scratch);
      return SPN_ERROR;
    }
    entry->path = subdir;
  }

  return SPN_OK;
}

spn_git_checkout_t* spn_git_cache_checkout(spn_git_cache_t* cache, spn_git_checkout_id_t id, bool* fetching) {
  sp_mutex_lock(&cache->mutex);
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();

  sp_str_t key = spn_git_checkout_key(scratch.mem, id);
  spn_git_checkout_t*** existing = sp_str_om_getp(cache->checkouts.entries, key);
  spn_git_checkout_t* entry = existing ? **existing : SP_NULLPTR;
  if (!entry) {
    sp_str_t owned = sp_str_copy(cache->mem, key);
    entry = sp_alloc_type(cache->mem, spn_git_checkout_t);
    entry->id = id;
    entry->path = spn_path_join(cache->mem, cache->checkouts.dir, owned);
    entry->fetched = !sp_fs_is_dir_at(spn_path_at(cache->roots, entry->path));
    sp_str_om_insert(cache->checkouts.entries, owned, entry);
  }

  sp_mem_end_scratch(scratch);
  sp_mutex_unlock(&cache->mutex);

  sp_mutex_lock(&entry->mutex);
  *fetching = entry->fetched && !entry->ready;
  sp_mutex_unlock(&entry->mutex);
  return entry;
}

spn_err_t spn_git_checkout_ensure(spn_git_cache_t* cache, spn_git_checkout_t* checkout, sp_atomic_s32_t* cancelled) {
  sp_mutex_lock(&checkout->mutex);
  if (!checkout->ready) {
    checkout->ready = true;
    checkout->err = spn_git_cache_materialize_checkout(cache, checkout, cancelled);
  }
  sp_mutex_unlock(&checkout->mutex);
  return checkout->err;
}
