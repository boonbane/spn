#include "spn/host.h"
#include "macro/macro.h"
#include "sp.h"

#include "ctx/types.h"
#include "spn/errors.h"
#include "index/types.h"

#include "error/error.h"
#include "event/event.h"
#include "external/git.h"
#include "external/git_cli.h"
#include "git/remote.h"
#include "git/key.h"
#include "index/dir.h"
#include "index/index.h"
#include "intern/intern.h"
#include "fs/fs.h"
#include "index/json.h"
#include "index/jsonl.h"
#include "semver/compare.h"
#include "semver/convert.h"
#include "io/io.h"
#include "paths/paths.h"
#include "str/str.h"

#define git_index_synced ".git/spn-synced"
#define git_index_failed ".git/spn-failed"

static sp_path_t git_index_stamp(sp_mem_t mem, spn_index_info_t* index, const c8* stamp) {
  return sp_path_join(mem, index->location.at, sp_cstr_as_str(stamp));
}

static spn_path_t git_index_staging(sp_mem_t mem, spn_index_info_t* index) {
  return spn_path_join(mem, spn_path_parent((spn_path_t) { .sub = index->location.dir }), sp_str_lit("t"));
}

static bool git_index_expired(spn_index_info_t* index, const c8* stamp) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_sys_file_meta_t meta = sp_zero;
  u64 modified = sp_fs_get_path_metadata_at(git_index_stamp(scratch.mem, index, stamp), &meta) ? 0 : (u64)meta.mtime.tv_sec;
  sp_mem_end_scratch(scratch);

  sp_tm_epoch_t now = sp_tm_now_epoch();
  return modified + index->refresh <= now.s;
}

static spn_err_t git_index_clone(spn_index_info_t* index, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_path_t base = git_index_staging(scratch.mem, index);
  spn_path_t staged = sp_zero;
  if (spn_path_stage_dir(scratch.mem, SP_NULLPTR, base, sp_str_lit("tmp"), &staged)) {
    *failure = (spn_git_failure_t) { .message = "failed to stage the clone" };
    sp_mem_end_scratch(scratch);
    return SPN_ERROR;
  }

  sp_path_t work = sp_path_from_str(staged.sub);
  sp_path_t old = sp_path_from_str(spn_path_staging(scratch.mem, base, sp_str_lit("old")).sub);
  spn_err_t err = spn_git_clone(index->remote, index->git.url, staged.sub, index->backoff, cancelled, failure);
  if (!err && sp_fs_replace_dir_at(work, index->location.at, old)) {
    *failure = (spn_git_failure_t) { .message = "failed to place the clone" };
    err = SPN_ERROR;
  }
  sp_fs_remove_dir_at(work);
  sp_mem_end_scratch(scratch);
  return err;
}

spn_index_plan_t spn_index_plan(spn_index_info_t* index, bool force) {
  sp_path_t at = index->location.at;
  sp_sys_file_meta_t meta = sp_zero;
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      bool present = !sp_fs_get_link_metadata_at(at, &meta) && meta.kind == SP_FS_KIND_DIR;
      bool due = sp_str_empty(index->git.rev) && git_index_expired(index, git_index_synced);
      bool held = present && !force && due && !git_index_expired(index, git_index_failed);
      return (spn_index_plan_t) {
        .present = present,
        .fetch = !present || force || (due && !held),
        .held = held,
        .force = force,
      };
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      return (spn_index_plan_t) { .force = force };
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      bool present = !sp_fs_get_link_metadata_at(at, &meta) && meta.kind == SP_FS_KIND_DIR;
      return (spn_index_plan_t) { .present = present, .force = force };
    }
  }
  sp_unreachable_return(sp_zero_s(spn_index_plan_t));
}

static spn_err_t git_index_sync(spn_index_info_t* index, spn_index_plan_t plan, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  sp_str_t dir = index->location.dir;
  if (!sp_str_empty(index->git.rev)) {
    if (!plan.present) {
      spn_try(git_index_clone(index, cancelled, failure));
    }
    else if (plan.fetch) {
      spn_try(spn_git_fetch(index->remote, dir, index->git.url, spn_net_no_backoff, cancelled, failure));
    }
    return spn_git_checkout(dir, index->git.rev);
  }
  if (!plan.fetch) {
    return plan.held ? SPN_ERROR : SPN_OK;
  }

  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = SPN_ERROR;

  sp_for(attempt, 2) {
    sp_str_t branch = sp_zero;
    if (attempt || !plan.present) {
      if (git_index_clone(index, cancelled, failure)) {
        break;
      }
    }
    else if (spn_git_fetch(index->remote, dir, index->git.url, spn_net_no_backoff, cancelled, failure)) {
      if (failure->kind == SPN_GIT_CORRUPT) {
        continue;
      }
      if (!spn_git_default_branch(scratch.mem, dir, &branch) && !spn_git_checkout_branch(dir, branch)) {
        spn_git_clean(dir);
      }
      break;
    }

    if (spn_git_default_branch(scratch.mem, dir, &branch)) {
      sp_str_t head = sp_zero;
      if (spn_git_has_remote_branches(dir) ||
          !spn_git_get_commit_full(scratch.mem, dir, sp_str_lit("HEAD"), &head)) {
        continue;
      }
    }
    else if (spn_git_checkout_branch(dir, branch)) {
      break;
    }
    if (spn_git_clean(dir)) {
      break;
    }

    sp_fs_write_at(git_index_stamp(scratch.mem, index, git_index_synced), sp_str_lit(""), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms);
    sp_fs_remove_file_at(git_index_stamp(scratch.mem, index, git_index_failed));
    err = SPN_OK;
    break;
  }

  if (err && failure->kind != SPN_GIT_CANCELLED) {
    sp_fs_write_at(git_index_stamp(scratch.mem, index, git_index_failed), sp_str_lit(""), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms);
  }

  sp_mem_end_scratch(scratch);
  return err;
}

spn_err_t spn_index_sync(spn_index_info_t* index, spn_index_plan_t plan, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  *failure = sp_zero_s(spn_git_failure_t);
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      sp_assert(index->remote.clone);
      sp_assert(index->remote.fetch);
      sp_assert(!index->backoff.wait_ms[0] || index->backoff.wait);
      if (sp_str_empty(index->git.rev) && !plan.fetch) {
        return plan.held ? SPN_ERROR : SPN_OK;
      }

      sp_fs_lock_t lock = sp_zero;
      sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
      sp_path_t at = index->location.at;
      spn_err_t err = spn_git_lock(index->location.lock, sp_path_join(scratch.mem, at, sp_str_lit(".git")), index->git.url, cancelled, &lock, failure);
      sp_mem_end_scratch(scratch);
      spn_try(err);

      err = git_index_sync(index, spn_index_plan(index, plan.force), cancelled, failure);
      sp_fs_lock_release(&lock);
      return err;
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      return SPN_ERROR;
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      return plan.present ? SPN_OK : SPN_ERROR;
    }
  }
  return SPN_ERROR;
}

static s32 sort_release_by_version(const void* a, const void* b) {
  const spn_index_release_t* lhs = (const spn_index_release_t*)a;
  const spn_index_release_t* rhs = (const spn_index_release_t*)b;
  return spn_semver_cmp(lhs->version, rhs->version);
}

bool spn_index_is_snapshot(const spn_index_info_t* index) {
  return index->protocol == SPN_INDEX_PROTOCOL_GIT && index->format == SPN_INDEX_FORMAT_MANIFESTS;
}

spn_err_t spn_index_get_package(spn_index_info_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err) {
  if (spn_index_is_snapshot(index) && sp_str_empty(index->git.rev)) {
    *err = (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_UNPINNED,
      .index = { .name = index->name.str, .url = index->git.url },
    };
    return err->kind;
  }

  switch (index->format) {
    case SPN_INDEX_FORMAT_RELEASES: {
      spn_try(spn_index_jsonl_get_package(index, mem, intern, name, pkg, err));
      break;
    }
    case SPN_INDEX_FORMAT_MANIFESTS: {
      spn_try(spn_index_dir_get_package(index, mem, intern, roots, name, pkg, err));
      break;
    }
  }

  if (*pkg) {
    sp_da_sort((*pkg)->releases, sort_release_by_version);
  }
  return SPN_OK;
}

static void index_append_release(spn_index_info_t* index, spn_index_release_t* rel) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();

  sp_path_t path = sp_path_join(scratch.mem, index->location.at, spn_index_jsonl_path(scratch.mem, rel->name));
  sp_fs_create_parent_at(path);

  sp_str_t json = spn_index_release_to_json(scratch.mem, rel);
  sp_fs_append_at(path, sp_fmt(scratch.mem, "{}\n", sp_fmt_str(json)).value, sp_sys_default_file_perms);

  sp_mem_end_scratch(scratch);
}

#define SPN_INDEX_PUBLISH_ATTEMPTS 3

static spn_err_t publish_from(sp_mem_t mem, spn_index_info_t* work, spn_index_release_t* rel, sp_str_t url, sp_atomic_s32_t* cancelled, sp_str_t* output) {
  sp_str_t dir = work->location.dir;

  spn_git_failure_t failure = sp_zero;
  if (spn_git_clone(work->remote, work->git.url, dir, work->backoff, cancelled, &failure)) {
    if (failure.kind == SPN_GIT_CANCELLED) {
      return SPN_ERR_CANCELLED;
    }
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_SYNC,
      .index = {
        .name = work->name.str,
        .url = work->git.url,
        .cause = sp_str_copy(spn.events->mem, sp_cstr_as_str(failure.message)),
      },
    });
  }

  spn_index_pkg_t* existing = SP_NULLPTR;
  spn_err_union_t err = sp_zero;
  if (spn_index_jsonl_get_package(work, spn.mem, spn.intern, rel->name, &existing, &err)) {
    return spn_err_emit(&spn, err);
  }
  if (existing) {
    sp_da_for(existing->releases, it) {
      if (spn_semver_eq(existing->releases[it].version, rel->version)) {
        return spn_err_emit(&spn, (spn_err_union_t) {
          .kind = SPN_ERR_VERSION_EXISTS,
          .version_exists = {
            .name = rel->name.str,
            .version = spn_semver_to_str(spn.events->mem, rel->version),
          },
        });
      }
    }
  }

  index_append_release(work, rel);

  // A clone of an empty remote has no origin/HEAD yet; the first push
  // creates the branch the clone was born on
  sp_str_t branch = sp_zero;
  if (spn_git_default_branch(mem, dir, &branch) &&
      spn_git_current_branch(mem, dir, &branch)) {
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_GIT,
      .git.command = sp_str_lit("git symbolic-ref HEAD"),
    });
  }

  if (spn_git_add(dir, sp_fs_join_path(mem, dir, spn_index_jsonl_path(mem, rel->name)))) {
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_GIT,
      .git.command = sp_str_lit("git add"),
    });
  }
  sp_str_t message = sp_fmt(mem, "{} {}",
    sp_fmt_str(rel->name.str),
    sp_fmt_str(spn_semver_to_str(mem, rel->version))).value;
  if (spn_git_commit(dir, message)) {
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_GIT,
      .git.command = sp_str_lit("git commit"),
    });
  }

  sp_str_t refspec = sp_fmt(mem, "HEAD:refs/heads/{}", sp_fmt_str(branch)).value;
  return spn_git_cli_push(mem, dir, url, refspec, output) ? SPN_ERR_PUBLISH_PUSH : SPN_OK;
}

static spn_err_t publish_git(spn_index_info_t* index, spn_index_release_t* rel, sp_atomic_s32_t* cancelled) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t url = spn_index_publish_target(index);
  sp_str_t output = sp_zero;
  spn_err_t result = SPN_ERR_PUBLISH_PUSH;

  sp_for(attempt, SPN_INDEX_PUBLISH_ATTEMPTS) {
    spn_path_t staged = sp_zero;
    if (spn_path_stage_dir(scratch.mem, SP_NULLPTR, git_index_staging(scratch.mem, index), sp_str_lit("pub"), &staged)) {
      result = spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_INDEX_SYNC,
        .index = {
          .name = index->name.str,
          .url = index->git.url,
          .cause = sp_str_lit("failed to stage the publish clone"),
        },
      });
      break;
    }

    spn_index_info_t work = *index;
    work.location.at = sp_path_from_str(staged.sub);
    work.location.dir = staged.sub;
    result = publish_from(scratch.mem, &work, rel, url, cancelled, &output);
    sp_fs_remove_dir_at(work.location.at);
    if (result != SPN_ERR_PUBLISH_PUSH) {
      break;
    }
  }

  if (result == SPN_ERR_PUBLISH_PUSH) {
    spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_PUBLISH_PUSH,
      .publish = {
        .url = url,
        .output = sp_str_copy(spn.events->mem, output),
      },
    });
  }
  sp_mem_end_scratch(scratch);
  return result;
}

spn_err_t spn_index_publish(spn_index_info_t* index, spn_index_release_t* rel, sp_atomic_s32_t* cancelled) {
  if (index->format == SPN_INDEX_FORMAT_MANIFESTS) {
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_PUBLISH_PROTOCOL,
      .index = { .name = index->name.str, .url = spn_index_source(index) },
    });
  }

  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      if (!sp_str_empty(index->git.rev)) {
        return spn_err_emit(&spn, (spn_err_union_t) {
          .kind = SPN_ERR_INDEX_PINNED,
          .index = { .name = index->name.str, .url = index->git.url },
        });
      }

      spn_try(publish_git(index, rel, cancelled));

      spn_index_plan_t plan = spn_index_plan(index, true);
      spn_git_failure_t failure = sp_zero;
      if (spn_index_sync(index, plan, cancelled, &failure)) {
        if (failure.kind == SPN_GIT_CANCELLED) {
          return SPN_ERR_CANCELLED;
        }
        if (plan.present) {
          spn_event_buffer_push(spn.events, (spn_event_t) {
            .kind = SPN_EVENT_SYNC_STALE,
            .sync = { .name = index->name.str, .url = spn_index_source(index) },
          });
        }
      }
      return SPN_OK;
    }

    case SPN_INDEX_PROTOCOL_HTTP:
    case SPN_INDEX_PROTOCOL_DIR: {
      return spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_INDEX_PUBLISH_PROTOCOL,
        .index = { .name = index->name.str, .url = spn_index_source(index) },
      });
    }
  }

  sp_unreachable_return(SPN_ERROR);
}

spn_index_location_t spn_index_location(spn_index_info_t* index, sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t store) {
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      sp_str_t key = spn_git_db_key(mem, index->git.url);
      if (!sp_str_empty(index->git.rev)) {
        key = spn_git_checkout_key(mem, (spn_git_checkout_id_t) { .url = index->git.url, .rev = index->git.rev });
      }
      spn_path_t dir = spn_path_join(mem, store, key);
      return (spn_index_location_t) { .dir = dir, .lock = spn_path_lock(mem, dir) };
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      return (spn_index_location_t) { .dir = spn_path_join(mem, store, spn_git_db_key(mem, index->http.url)) };
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      return (spn_index_location_t) { .dir = spn_path_canonicalize(mem, roots, index->dir.path) };
    }
  }
  sp_unreachable_return(sp_zero_struct(spn_index_location_t));
}

void spn_index_locate(spn_index_info_t* index, sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t store) {
  spn_index_location_t location = spn_index_location(index, mem, roots, store);
  index->location.at = spn_path_at(roots, location.dir);
  index->location.dir = spn_path_str(roots, mem, location.dir);
  index->location.lock = spn_path_at(roots, location.lock);
}

static spn_index_info_t* find_index(sp_da(spn_index_info_t)* indexes, spn_index_id_t id) {
  sp_da_for(*indexes, it) {
    if ((*indexes)[it].id == id) {
      return &(*indexes)[it];
    }
  }
  return SP_NULLPTR;
}

void spn_index_assemble(sp_mem_t mem, spn_index_map_t* workspace, sp_da(spn_index_info_t) user, sp_da(spn_index_info_t)* indexes) {
  *indexes = sp_da_new(mem, spn_index_info_t);

  if (workspace) {
    si_om_for(*workspace, it) {
      sp_da_push(*indexes, *si_om_at(*workspace, it));
    }
  }

  sp_da_for(user, it) {
    if (!find_index(indexes, user[it].id)) {
      sp_da_push(*indexes, user[it]);
    }
  }

  sp_intern_str_t core = spn_intern_lit("core");
  if (!find_index(indexes, core.id)) {
    sp_da_push(*indexes, ((spn_index_info_t) {
      .id = core.id,
      .name = core,
      .protocol = SPN_INDEX_PROTOCOL_GIT,
      .kind = SPN_INDEX_KIND_BUILTIN,
      .git = { .url = sp_str_lit("https://github.com/tspader/spandex.git") },
    }));
  }
}

sp_str_t spn_index_publish_target(spn_index_info_t* index) {
  if (index->protocol == SPN_INDEX_PROTOCOL_GIT && !sp_str_empty(index->git.publish_url)) {
    return index->git.publish_url;
  }
  return spn_index_source(index);
}

sp_str_t spn_index_source(spn_index_info_t* index) {
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      return index->git.url;
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      return index->http.url;
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      return index->location.dir;
    }
  }
  sp_unreachable_return(sp_str_lit(""));
}

