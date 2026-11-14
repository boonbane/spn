#include "spn/host.h"
#include "macro/macro.h"
#include "sp.h"

#include "ctx/types.h"
#include "spn/errors.h"
#include "index/types.h"

#include "error/error.h"
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
  sp_tm_epoch_t mod_time = sp_fs_get_mod_time_at(git_index_stamp(scratch.mem, index, stamp));
  sp_mem_end_scratch(scratch);

  sp_tm_epoch_t now = sp_tm_now_epoch();
  return mod_time.s + index->refresh <= now.s;
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
  spn_err_t err = spn_git_clone(index->transport, index->git.url, staged.sub, index->backoff, cancelled, failure);
  if (!err && sp_fs_replace_dir_at(work, index->location.at, old)) {
    *failure = (spn_git_failure_t) { .message = "failed to place the clone" };
    err = SPN_ERROR;
  }
  sp_fs_remove_dir_at(work);
  sp_mem_end_scratch(scratch);
  return err;
}

static spn_err_t git_index_reset(spn_index_info_t* index) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t dir = index->location.dir;
  sp_str_t branch = sp_zero;
  bool reset =
    !spn_git_default_branch(scratch.mem, dir, &branch) &&
    !spn_git_checkout_branch(dir, branch) &&
    !spn_git_clean(dir);
  sp_mem_end_scratch(scratch);
  return reset ? SPN_OK : SPN_ERROR;
}

static spn_err_t git_index_freshen(spn_index_info_t* index, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = SPN_ERROR;
  sp_path_t at = index->location.at;
  sp_str_t dir = index->location.dir;

  sp_for(attempt, 2) {
    if (attempt || !sp_fs_is_dir_at(at)) {
      if (git_index_clone(index, cancelled, failure)) {
        break;
      }
    }
    else if (spn_git_fetch(index->transport, dir, index->git.url, spn_net_no_backoff, cancelled, failure)) {
      if (failure->kind == SPN_GIT_CORRUPT) {
        continue;
      }
      git_index_reset(index);
      break;
    }

    sp_str_t branch = sp_zero;
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

    sp_fs_create_file_at(git_index_stamp(scratch.mem, index, git_index_synced));
    sp_fs_remove_file_at(git_index_stamp(scratch.mem, index, git_index_failed));
    err = SPN_OK;
    break;
  }

  if (err && failure->kind != SPN_GIT_CANCELLED) {
    sp_fs_create_file_at(git_index_stamp(scratch.mem, index, git_index_failed));
  }

  sp_mem_end_scratch(scratch);
  return err;
}

static spn_err_t git_index_lock(spn_index_info_t* index, sp_atomic_s32_t* cancelled, sp_fs_lock_t* lock, spn_git_failure_t* failure) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_path_t at = index->location.at;
  spn_err_t err = spn_git_lock(at, sp_path_join(scratch.mem, at, sp_str_lit(".git")), index->git.url, cancelled, lock, failure);
  sp_mem_end_scratch(scratch);
  return err;
}

spn_index_plan_t spn_index_plan(spn_index_info_t* index, bool force) {
  sp_path_t at = index->location.at;
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      bool present = sp_fs_exists_at(at);
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
      return (spn_index_plan_t) { .present = sp_fs_is_dir_at(at), .force = force };
    }
  }
  sp_unreachable_return(sp_zero_s(spn_index_plan_t));
}

static spn_err_t git_index_sync(spn_index_info_t* index, spn_index_plan_t plan, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  sp_str_t dir = index->location.dir;
  if (sp_str_empty(index->git.rev)) {
    if (!plan.fetch) {
      return plan.held ? SPN_ERROR : SPN_OK;
    }
    return git_index_freshen(index, cancelled, failure);
  }
  if (!plan.present) {
    spn_try(git_index_clone(index, cancelled, failure));
  }
  else if (plan.fetch) {
    spn_try(spn_git_fetch(index->transport, dir, index->git.url, spn_net_no_backoff, cancelled, failure));
  }
  return spn_git_checkout(dir, index->git.rev);
}

spn_err_t spn_index_sync(spn_index_info_t* index, spn_index_plan_t plan, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure) {
  *failure = sp_zero_s(spn_git_failure_t);
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      if (sp_str_empty(index->git.rev) && !plan.fetch) {
        return plan.held ? SPN_ERROR : SPN_OK;
      }

      sp_fs_lock_t lock = sp_zero;
      spn_try(git_index_lock(index, cancelled, &lock, failure));
      spn_err_t err = git_index_sync(index, spn_index_plan(index, plan.force), cancelled, failure);
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

spn_err_t spn_index_get_package(spn_index_info_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err) {
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT:
    case SPN_INDEX_PROTOCOL_HTTP: {
      return spn_index_jsonl_get_package(index, mem, intern, name, pkg, err);
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      return spn_index_dir_get_package(index, mem, intern, roots, name, pkg, err);
    }
  }
  sp_unreachable_return(SPN_ERROR);
}

static void index_append_release(spn_index_info_t* index, spn_index_release_t* rel) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();

  sp_path_t path = sp_path_join(scratch.mem, index->location.at, spn_index_jsonl_path(scratch.mem, rel->name));
  sp_fs_create_parent_at(path);

  sp_str_t json = spn_index_release_to_json(scratch.mem, rel);

  sp_sys_fd_t fd = SP_SYS_INVALID_FD;
  sp_sys_open_s(path.dir, path.sub, SP_SYS_OPEN_MODE_WO, SP_SYS_OPEN_CREATE | SP_SYS_OPEN_APPEND, &fd);
  sp_io_stream_writer_t io;
  sp_io_stream_writer_from_fd(&io, fd, SP_IO_CLOSE_MODE_AUTO);
  u8 buffer [4096] = sp_zero;
  sp_io_writer_set_buffer(&io.base, buffer, sizeof(buffer));
  sp_io_write_line(&io.base, json);
  sp_io_stream_writer_close(&io);

  sp_mem_end_scratch(scratch);
}

#define SPN_INDEX_PUBLISH_ATTEMPTS 3

static spn_err_t publish_git(spn_index_info_t* index, spn_index_release_t* rel, sp_atomic_s32_t* cancelled, bool* dirtied) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t message = sp_fmt(scratch.mem, "{} {}",
    sp_fmt_str(rel->name.str),
    sp_fmt_str(spn_semver_to_str(scratch.mem, rel->version))).value;
  sp_str_t url = spn_index_publish_target(index);
  sp_str_t dir = index->location.dir;

  spn_err_t result = SPN_OK;
  sp_str_t output = sp_zero;

  sp_for(attempt, SPN_INDEX_PUBLISH_ATTEMPTS) {
    spn_git_failure_t failure = sp_zero;
    if (git_index_freshen(index, cancelled, &failure)) {
      if (failure.kind == SPN_GIT_CANCELLED) {
        result = SPN_ERR_CANCELLED;
        break;
      }
      result = spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_INDEX_SYNC,
        .index = {
          .name = index->name.str,
          .url = index->git.url,
          .cause = sp_str_copy(spn.events->mem, sp_cstr_as_str(failure.message)),
        },
      });
      break;
    }
    *dirtied = false;

    spn_index_pkg_t* existing = SP_NULLPTR;
    spn_err_union_t err = sp_zero;
    if (spn_index_jsonl_get_package(index, spn.mem, spn.intern, rel->name, &existing, &err)) {
      result = spn_err_emit(&spn, err);
      break;
    }

    bool exists = false;
    if (existing) {
      sp_da_for(existing->releases, it) {
        if (spn_semver_eq(existing->releases[it].version, rel->version)) {
          exists = true;
          break;
        }
      }
    }
    if (exists) {
      result = spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_VERSION_EXISTS,
        .version_exists = {
          .name = rel->name.str,
          .version = spn_semver_to_str(spn.events->mem, rel->version),
        },
      });
      break;
    }

    sp_fs_remove_file_at(git_index_stamp(scratch.mem, index, git_index_synced));
    index_append_release(index, rel);
    *dirtied = true;

    // A clone of an empty remote has no origin/HEAD yet; the first push
    // creates the branch the clone was born on
    sp_str_t branch = sp_zero;
    if (spn_git_default_branch(scratch.mem, dir, &branch) &&
        spn_git_current_branch(scratch.mem, dir, &branch)) {
      result = spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_GIT,
        .git.command = sp_str_lit("git symbolic-ref HEAD"),
      });
      break;
    }

    sp_str_t path = sp_fs_join_path(scratch.mem, dir, spn_index_jsonl_path(scratch.mem, rel->name));
    if (spn_git_add(dir, path)) {
      result = spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_GIT,
        .git.command = sp_str_lit("git add"),
      });
      break;
    }
    if (spn_git_commit(dir, message)) {
      result = spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_GIT,
        .git.command = sp_str_lit("git commit"),
      });
      break;
    }

    sp_str_t refspec = sp_fmt(scratch.mem, "HEAD:refs/heads/{}", sp_fmt_str(branch)).value;
    if (!spn_git_cli_push(scratch.mem, dir, url, refspec, &output)) {
      sp_fs_create_file_at(git_index_stamp(scratch.mem, index, git_index_synced));
      sp_mem_end_scratch(scratch);
      return SPN_OK;
    }

    result = SPN_ERR_PUBLISH_PUSH;
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
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      if (!sp_str_empty(index->git.rev)) {
        return spn_err_emit(&spn, (spn_err_union_t) {
          .kind = SPN_ERR_INDEX_PINNED,
          .index = { .name = index->name.str, .url = index->git.url },
        });
      }

      sp_fs_lock_t lock = sp_zero;
      spn_git_failure_t refused = sp_zero;
      if (git_index_lock(index, cancelled, &lock, &refused)) {
        if (refused.kind == SPN_GIT_CANCELLED) {
          return SPN_ERR_CANCELLED;
        }
        return spn_err_emit(&spn, (spn_err_union_t) {
          .kind = SPN_ERR_INDEX_SYNC,
          .index = {
            .name = index->name.str,
            .url = index->git.url,
            .cause = sp_str_copy(spn.events->mem, sp_cstr_as_str(refused.message)),
          },
        });
      }

      bool dirtied = false;
      spn_err_t err = publish_git(index, rel, cancelled, &dirtied);

      // Leave the replica clean when the transaction died after mutating it
      if (err && dirtied && git_index_reset(index)) {
        sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
        sp_path_t at = index->location.at;
        sp_path_t old = sp_path_from_str(spn_path_staging(scratch.mem, git_index_staging(scratch.mem, index), sp_str_lit("old")).sub);
        if (!sp_sys_rename_s(at.dir, at.sub, old.dir, old.sub)) {
          sp_fs_remove_dir_at(old);
        }
        sp_mem_end_scratch(scratch);
      }
      sp_fs_lock_release(&lock);
      return err;
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

spn_path_t spn_index_location(spn_index_info_t* index, sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t store) {
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      return spn_path_join(mem, store, spn_git_db_key(mem, index->git.url));
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      return spn_path_join(mem, store, spn_git_db_key(mem, index->http.url));
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      return spn_path_canonicalize(mem, roots, index->dir.path);
    }
  }
  sp_unreachable_return(sp_zero_struct(spn_path_t));
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

