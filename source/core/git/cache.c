#include "sp.h"
#include "macro/macro.h"
#include "git/cache.h"

#include "external/git.h"
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

spn_err_t spn_git_cache_ensure_db(spn_git_cache_t* cache, sp_str_t url, spn_git_db_t** db) {
  spn_git_db_t* entry = spn_git_cache_db_entry(cache, url);
  *db = entry;

  sp_mutex_lock(&entry->mutex);
  if (!entry->ready) {
    entry->ready = true;

    if (!sp_fs_is_dir_at(spn_path_at(cache->roots, entry->path))) {
      sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
      sp_ps_output_t result = sp_ps_run(scratch.mem, (sp_ps_config_t) {
        .command = SP_LIT("git"),
        .args = {
          SP_LIT("clone"), SP_LIT("--bare"), SP_LIT("--quiet"),
          SP_LIT("-c"), SP_LIT("core.autocrlf=false"),
          url,
          spn_path_str(cache->roots, scratch.mem, entry->path)
        },
        .io.err.mode = SP_PS_IO_MODE_REDIRECT,
      });

      if (result.status.exit_code) {
        entry->err = SPN_ERROR;
        entry->error = sp_str_copy(cache->mem, sp_str_trim_right(result.out));
      }
      sp_mem_end_scratch(scratch);
    }
  }
  sp_mutex_unlock(&entry->mutex);

  return entry->err;
}

spn_err_t spn_git_db_ensure_rev(spn_git_cache_t* cache, spn_git_db_t* db, sp_str_t rev) {
  sp_mutex_lock(&db->mutex);
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t path = spn_path_str(cache->roots, scratch.mem, db->path);

  sp_ps_config_t cat_file = {
    .command = SP_LIT("git"),
    .args = {
      SP_LIT("-C"), path,
      SP_LIT("cat-file"), SP_LIT("-t"), rev
    },
    .io.err.mode = SP_PS_IO_MODE_NULL,
  };

  sp_ps_output_t result = sp_ps_run(scratch.mem, cat_file);

  if (result.status.exit_code) {
    // Bare clones have no remote.origin.fetch refspec, so a plain fetch never
    // brings in commits made after the clone. Ask for the rev itself, and if
    // the remote won't serve it by hash, mirror all heads and tags.
    result = sp_ps_run(scratch.mem, (sp_ps_config_t) {
      .command = SP_LIT("git"),
      .args = {
        SP_LIT("-C"), path,
        SP_LIT("fetch"), SP_LIT("--quiet"), SP_LIT("origin"), rev
      },
      .io.err.mode = SP_PS_IO_MODE_NULL,
    });

    if (result.status.exit_code) {
      result = sp_ps_run(scratch.mem, (sp_ps_config_t) {
        .command = SP_LIT("git"),
        .args = {
          SP_LIT("-C"), path,
          SP_LIT("fetch"), SP_LIT("--quiet"), SP_LIT("origin"),
          SP_LIT("+refs/heads/*:refs/heads/*"), SP_LIT("+refs/tags/*:refs/tags/*")
        },
        .io.err.mode = SP_PS_IO_MODE_NULL,
      });
    }

    if (!result.status.exit_code) {
      result = sp_ps_run(scratch.mem, cat_file);
    }
  }

  sp_mem_end_scratch(scratch);
  sp_mutex_unlock(&db->mutex);
  return result.status.exit_code ? SPN_ERROR : SPN_OK;
}

static sp_str_t checkout_error(spn_git_cache_t* cache, const c8* what, spn_path_t path) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t error = sp_fmt(cache->mem, "failed to {} checkout at {}", sp_fmt_cstr(what), sp_fmt_str(spn_path_str(cache->roots, scratch.mem, path))).value;
  sp_mem_end_scratch(scratch);
  return error;
}

static spn_err_t spn_git_cache_fill_checkout(spn_git_cache_t* cache, spn_git_checkout_t* entry, spn_git_db_t* db, spn_path_t staged) {
  sp_str_buf_t buf = sp_zero;
  sp_str_t work = spn_path_str(cache->roots, sp_str_buf_as_mem(&buf), staged);
  // Checkouts must be byte-identical to the committed content no matter
  // what the machine's autocrlf is; hashes and golden comparisons depend
  // on it. -c on clone persists into the new repo's config.
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_ps_output_t result = sp_ps_run(scratch.mem, (sp_ps_config_t) {
    .command = SP_LIT("git"),
    .args = {
      SP_LIT("clone"), SP_LIT("--shared"), SP_LIT("--quiet"),
      SP_LIT("-c"), SP_LIT("core.autocrlf=false"),
      spn_path_str(cache->roots, scratch.mem, db->path),
      work
    },
    .io.err.mode = SP_PS_IO_MODE_REDIRECT,
  });
  spn_err_t clone = result.status.exit_code ? SPN_ERROR : SPN_OK;
  if (clone) {
    entry->error = sp_str_copy(cache->mem, sp_str_trim_right(result.out));
  }
  sp_mem_end_scratch(scratch);
  if (clone) {
    return clone;
  }

  if (spn_git_checkout(work, entry->id.rev)) {
    entry->error = sp_fmt(cache->mem, "failed to check out {}", sp_fmt_str(entry->id.rev)).value;
    return SPN_ERROR;
  }

  sp_da_for(entry->id.patches.files, it) {
    sp_mem_arena_marker_t s = sp_mem_begin_scratch();
    sp_str_t error = sp_zero;
    spn_err_t applied = spn_git_apply(s.mem, work, entry->id.patches.files[it], &error);
    if (applied) {
      entry->error = sp_fmt(cache->mem, "failed to apply {}: {}",
        sp_fmt_str(entry->id.patches.files[it]), sp_fmt_str(error)).value;
    }
    sp_mem_end_scratch(s);
    if (applied) {
      return applied;
    }
  }

  return SPN_OK;
}

static spn_err_t spn_git_cache_materialize_checkout(spn_git_cache_t* cache, spn_git_checkout_t* entry) {
  spn_git_db_t* db = SP_NULLPTR;
  if (spn_git_cache_ensure_db(cache, entry->id.url, &db)) {
    entry->error = db->error;
    return SPN_ERROR;
  }

  if (spn_git_db_ensure_rev(cache, db, entry->id.rev)) {
    entry->error = sp_fmt(cache->mem, "{} has no rev {}", sp_fmt_str(entry->id.url), sp_fmt_str(entry->id.rev)).value;
    return SPN_ERROR;
  }

  sp_path_t dest = spn_path_at(cache->roots, entry->path);
  if (!sp_fs_is_dir_at(dest)) {
    entry->fetched = true;

    // Fill a claimed staging dir and rename into place, so a crash never
    // leaves a partial tree that later runs mistake for a finished checkout
    spn_path_t staged = sp_zero;
    if (spn_path_stage_dir(cache->mem, cache->roots, entry->path, sp_str_lit("tmp"), &staged)) {
      entry->error = checkout_error(cache, "stage", entry->path);
      return SPN_ERROR;
    }

    sp_path_t work = spn_path_at(cache->roots, staged);
    if (spn_git_cache_fill_checkout(cache, entry, db, staged)) {
      sp_fs_remove_dir_at(work);
      return SPN_ERROR;
    }

    if (sp_sys_rename_s(work.dir, work.sub, dest.dir, dest.sub)) {
      sp_fs_remove_dir_at(work);
      if (!sp_fs_is_dir_at(dest)) {
        entry->error = checkout_error(cache, "place", entry->path);
        return SPN_ERROR;
      }
    }
  }

  if (!sp_str_empty(entry->id.dir)) {
    spn_path_t subdir = spn_path_join(cache->mem, entry->path, entry->id.dir);
    if (!sp_fs_is_dir_at(spn_path_at(cache->roots, subdir))) {
      entry->error = sp_fmt(cache->mem, "{} does not exist in {}", sp_fmt_str(entry->id.dir), sp_fmt_str(entry->id.url)).value;
      return SPN_ERROR;
    }
    entry->path = subdir;
  }

  return SPN_OK;
}

spn_err_t spn_git_cache_ensure_checkout(spn_git_cache_t* cache, spn_git_checkout_id_t id, spn_git_checkout_t** checkout) {
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
    sp_str_om_insert(cache->checkouts.entries, owned, entry);
  }

  sp_mem_end_scratch(scratch);
  sp_mutex_unlock(&cache->mutex);

  sp_mutex_lock(&entry->mutex);
  if (!entry->ready) {
    entry->ready = true;
    entry->err = spn_git_cache_materialize_checkout(cache, entry);
  }
  sp_mutex_unlock(&entry->mutex);

  *checkout = entry;
  return entry->err;
}

bool spn_git_cache_is_checkout_cached(spn_git_cache_t* cache, spn_git_checkout_id_t id) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t key = spn_git_checkout_key(scratch.mem, id);
  bool cached = sp_fs_is_dir_at(spn_path_at(cache->roots, spn_path_join(scratch.mem, cache->checkouts.dir, key)));
  sp_mem_end_scratch(scratch);
  return cached;
}
