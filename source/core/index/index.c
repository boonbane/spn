#include "spn/host.h"
#include "macro/macro.h"
#include "sp.h"

#include "ctx/types.h"
#include "spn/errors.h"
#include "index/types.h"

#include "error/error.h"
#include "external/git.h"
#include "git/key.h"
#include "index/dir.h"
#include "index/index.h"
#include "fs/fs.h"
#include "index/json.h"
#include "index/jsonl.h"
#include "pkg/id.h"
#include "semver/compare.h"
#include "semver/convert.h"
#include "io/io.h"
#include "paths/paths.h"

static sp_str_t location(sp_mem_t mem, spn_index_info_t* index) {
  return spn_path_str(&spn.roots, mem, index->location);
}

static bool git_index_stale(spn_index_info_t* index) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_path_t head = spn_path_join(scratch.mem, index->location, sp_str_lit(".git/FETCH_HEAD"));
  sp_tm_epoch_t mod_time = sp_fs_get_mod_time_at(spn_path_at(&spn.roots, head));
  sp_mem_end_scratch(scratch);

  sp_tm_epoch_t now = sp_tm_now_epoch();
  return mod_time.s + index->refresh <= now.s;
}

static spn_err_t git_index_freshen(spn_index_info_t* index) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = SPN_ERROR;
  sp_path_t at = spn_path_at(&spn.roots, index->location);
  sp_str_t dir = location(scratch.mem, index);

  sp_for(attempt, 2) {
    if (attempt || !sp_fs_is_dir_at(at)) {
      sp_fs_remove_dir_at(at);
      if (spn_git_clone(index->git.url, dir)) {
        break;
      }
    }
    else if (spn_git_fetch(dir)) {
      continue;
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
      continue;
    }
    if (spn_git_clean(dir)) {
      continue;
    }

    err = SPN_OK;
    break;
  }

  sp_mem_end_scratch(scratch);
  return err;
}

spn_err_t spn_index_sync(spn_index_info_t* index, bool force) {
  sp_path_t at = spn_path_at(&spn.roots, index->location);
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      bool pinned = !sp_str_empty(index->git.rev);
      sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
      sp_str_t dir = location(scratch.mem, index);
      spn_err_t err = SPN_OK;

      if (sp_fs_exists_at(at)) {
        if (pinned) {
          err = force ? spn_git_fetch(dir) : SPN_OK;
          if (!err) {
            err = spn_git_checkout(dir, index->git.rev);
          }
        }
        else if (force || git_index_stale(index)) {
          err = git_index_freshen(index);
        }
      }
      else {
        err = spn_git_clone(index->git.url, dir);
        if (!err && pinned) {
          err = spn_git_checkout(dir, index->git.rev);
        }
      }
      sp_mem_end_scratch(scratch);
      return err;
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      return SPN_ERROR;
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      return sp_fs_is_dir_at(at) ? SPN_OK : SPN_ERROR;
    }
  }
  return SPN_ERROR;
}

bool spn_index_needs_fetch(spn_index_info_t* index) {
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      if (!sp_fs_exists_at(spn_path_at(&spn.roots, index->location))) {
        return true;
      }

      bool pinned = !sp_str_empty(index->git.rev);
      if (pinned) {
        return false;
      }

      return git_index_stale(index);
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      return false;
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      return false;
    }
  }
  return false;
}

spn_err_t spn_index_get_package(spn_index_info_t* index, sp_mem_t mem, sp_intern_t* intern, spn_pkg_name_t id, spn_index_pkg_t** pkg, spn_index_diag_t* diag) {
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT:
    case SPN_INDEX_PROTOCOL_HTTP: {
      return spn_index_jsonl_get_package(index, mem, id, pkg, diag);
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      return spn_index_dir_get_package(index, mem, intern, id, pkg, diag);
    }
  }
  sp_unreachable_return(SPN_ERROR);
}

static spn_err_t index_release_exists(spn_index_info_t* index, sp_mem_t mem, spn_index_release_t* rel, bool* exists, spn_index_diag_t* diag) {
  *exists = false;

  spn_index_pkg_t* existing = SP_NULLPTR;
  spn_try(spn_index_jsonl_get_package(index, mem, rel->id, &existing, diag));
  if (!existing) {
    return SPN_OK;
  }

  sp_da_for(existing->releases, it) {
    if (spn_semver_eq(existing->releases[it].version, rel->version)) {
      *exists = true;
      break;
    }
  }
  return SPN_OK;
}

static void index_append_release(spn_index_info_t* index, spn_index_release_t* rel) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();

  sp_path_t path = spn_path_at(&spn.roots, spn_index_jsonl_path(scratch.mem, index, rel->id));
  sp_fs_create_parent(path);

  sp_str_t json = spn_index_release_to_json(scratch.mem, rel);

  sp_sys_fd_t fd = SP_SYS_INVALID_FD;
  sp_sys_open_s(path.dir, path.sub, SP_SYS_OPEN_MODE_WO, SP_SYS_OPEN_CREATE | SP_SYS_OPEN_APPEND, &fd);
  sp_io_stream_writer_t io;
  sp_io_stream_writer_from_fd(&io, fd, SP_IO_CLOSE_MODE_AUTO);
  sp_io_write_line(&io.base, json);
  sp_io_stream_writer_close(&io);

  sp_mem_end_scratch(scratch);
}

#define SPN_INDEX_PUBLISH_ATTEMPTS 3

static spn_err_t publish_git(spn_index_info_t* index, sp_mem_t mem, spn_index_release_t* rel, bool* dirtied) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_str_t message = sp_fmt(scratch.mem, "{}/{} {}",
    sp_fmt_str(rel->id.namespace),
    sp_fmt_str(rel->id.name),
    sp_fmt_str(spn_semver_to_str(scratch.mem, rel->version))).value;
  sp_str_t url = spn_index_publish_target(index);
  sp_str_t dir = location(scratch.mem, index);

  spn_err_t result = SPN_OK;
  sp_str_t output = sp_zero;

  sp_for(attempt, SPN_INDEX_PUBLISH_ATTEMPTS) {
    if (git_index_freshen(index)) {
      result = spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_INDEX_SYNC,
        .index = { .name = index->name, .url = index->git.url },
      });
      break;
    }
    *dirtied = false;

    bool exists = false;
    spn_index_diag_t diag = sp_zero;
    if (index_release_exists(index, scratch.mem, rel, &exists, &diag)) {
      result = spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_INDEX_CORRUPT,
        .index_corrupt = { .name = spn_pkg_name_to_qualified(rel->id), .path = sp_str_copy(mem, diag.path) },
      });
      break;
    }
    if (exists) {
      result = spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_VERSION_EXISTS,
        .version_exists = {
          .name = rel->id.name,
          .version = spn_semver_to_str(mem, rel->version),
        },
      });
      break;
    }

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

    sp_str_t path = spn_path_str(&spn.roots, scratch.mem, spn_index_jsonl_path(scratch.mem, index, rel->id));
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
    if (!spn_git_push(scratch.mem, dir, url, refspec, &output)) {
      sp_mem_end_scratch(scratch);
      return SPN_OK;
    }

    result = SPN_ERR_PUBLISH_PUSH;
    output = sp_str_copy(mem, output);
  }

  if (result == SPN_ERR_PUBLISH_PUSH) {
    spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_PUBLISH_PUSH,
      .publish = {
        .url = sp_str_copy(mem, url),
        .output = output,
      },
    });
  }
  sp_mem_end_scratch(scratch);
  return result;
}

spn_err_t spn_index_publish(spn_index_info_t* index, sp_mem_t mem, spn_index_release_t* rel) {
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      if (!sp_str_empty(index->git.rev)) {
        return spn_err_emit(&spn, (spn_err_union_t) {
          .kind = SPN_ERR_INDEX_PINNED,
          .index = { .name = index->name, .url = index->git.url },
        });
      }

      bool dirtied = false;
      spn_err_t err = publish_git(index, mem, rel, &dirtied);

      // Leave the replica clean when the transaction died after mutating it
      if (err && dirtied) {
        git_index_freshen(index);
      }
      return err;
    }

    case SPN_INDEX_PROTOCOL_HTTP:
    case SPN_INDEX_PROTOCOL_DIR: {
      return spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_INDEX_PUBLISH_PROTOCOL,
        .index = { .name = index->name, .url = spn_index_source(index) },
      });
    }
  }

  sp_unreachable_return(SPN_ERROR);
}

spn_path_t spn_index_location(spn_index_info_t* index, sp_mem_t mem, spn_path_t root) {
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      return spn_path_join(mem, root, spn_git_db_key(mem, index->git.url));
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      return spn_path_join(mem, root, spn_git_db_key(mem, index->http.url));
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      return (spn_path_t) { .sub = index->dir.path };
    }
  }
  sp_unreachable_return(sp_zero_struct(spn_path_t));
}

static spn_index_info_t* find_index(sp_da(spn_index_info_t)* indexes, sp_str_t name) {
  sp_da_for(*indexes, it) {
    if (sp_str_equal((*indexes)[it].name, name)) {
      return &(*indexes)[it];
    }
  }
  return SP_NULLPTR;
}

void spn_index_assemble(sp_mem_t mem, spn_index_map_t* workspace, sp_da(spn_index_info_t) user, sp_da(spn_index_info_t)* indexes) {
  *indexes = sp_da_new(mem, spn_index_info_t);

  if (workspace) {
    sp_str_om_for(*workspace, it) {
      sp_da_push(*indexes, *sp_str_om_at(*workspace, it));
    }
  }

  sp_da_for(user, it) {
    if (!find_index(indexes, user[it].name)) {
      sp_da_push(*indexes, user[it]);
    }
  }

  if (!find_index(indexes, sp_str_lit("core"))) {
    sp_da_push(*indexes, ((spn_index_info_t) {
      .name = sp_str_lit("core"),
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
      return index->dir.path;
    }
  }
  sp_unreachable_return(sp_str_lit(""));
}

