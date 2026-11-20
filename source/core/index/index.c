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
#include "index/marker.h"
#include "index/tip.h"
#include "lock/lock.h"
#include "semver/compare.h"
#include "semver/convert.h"
#include "io/io.h"
#include "paths/paths.h"
#include "str/str.h"

static s32 sort_release_by_version(const void* a, const void* b) {
  const spn_index_release_t* lhs = (const spn_index_release_t*)a;
  const spn_index_release_t* rhs = (const spn_index_release_t*)b;
  return spn_semver_cmp(lhs->version, rhs->version);
}

sp_str_t si_index_pin(const spn_index_info_t* index, const spn_lock_t* lock) {
  if (!sp_str_empty(index->git.rev)) {
    return index->git.rev;
  }

  const spn_lock_index_t* held = spn_lock_find_index(lock, index->id);
  if (held && sp_str_equal(held->url, index->git.url)) {
    return held->rev;
  }
  return sp_str_lit("");
}

spn_err_t si_index_read_marker(spn_index_info_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_path_t dir, si_index_marker_t* marker, spn_err_union_t* err) {
  spn_try(si_index_marker_load(mem, intern, roots, dir, marker, err));
  if (marker->version) {
    return SPN_OK;
  }
  if (index->protocol == SPN_INDEX_PROTOCOL_DIR) {
    *marker = (si_index_marker_t) { .version = si_index_marker_version, .format = SPN_INDEX_FORMAT_MANIFESTS };
    return SPN_OK;
  }

  *err = (spn_err_union_t) {
    .kind = SPN_ERR_INDEX_UNMARKED,
    .index = { .name = index->name.str, .url = spn_index_source(index) },
  };
  return err->kind;
}

spn_err_t si_index_get_package(const si_index_place_t* index, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_intern_str_t name, spn_index_pkg_t** pkg, spn_err_union_t* err) {
  switch (index->format) {
    case SPN_INDEX_FORMAT_RELEASES: {
      spn_try(si_index_jsonl_get_package(index, mem, intern, name, pkg, err));
      break;
    }
    case SPN_INDEX_FORMAT_MANIFESTS: {
      spn_try(si_index_dir_get_package(index, mem, intern, roots, name, pkg, err));
      break;
    }
  }

  if (*pkg) {
    sp_da_sort((*pkg)->releases, sort_release_by_version);
  }
  return SPN_OK;
}

#define SPN_INDEX_PUBLISH_ATTEMPTS 3

static spn_err_t publish_from(sp_mem_t mem, spn_index_info_t* index, sp_str_t dir, spn_index_release_t* rel, sp_str_t url, sp_atomic_s32_t* cancelled, sp_str_t* output) {
  spn_git_failure_t failure = sp_zero;
  if (si_git_clone(index->remote, index->git.url, dir, index->backoff, cancelled, &failure)) {
    if (failure.kind == SPN_GIT_CANCELLED) {
      return SPN_ERR_CANCELLED;
    }
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_SYNC,
      .index = {
        .name = index->name.str,
        .url = index->git.url,
        .cause = sp_str_copy(spn.events->mem, sp_cstr_as_str(failure.message)),
      },
    });
  }

  sp_str_t branch = sp_zero;
  bool empty = spn_git_default_branch(mem, dir, &branch) != SPN_OK;
  if (empty && spn_git_current_branch(mem, dir, &branch)) {
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_GIT,
      .git.command = sp_str_lit("git symbolic-ref HEAD"),
    });
  }

  spn_path_t staged = { .sub = dir };
  si_index_marker_t marker = sp_zero;
  spn_err_union_t err = sp_zero;
  if (si_index_marker_load(spn.mem, spn.intern, &spn.roots, staged, &marker, &err)) {
    return spn_err_emit(&spn, err);
  }
  if (!marker.version) {
    if (!empty) {
      return spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_INDEX_UNMARKED,
        .index = { .name = index->name.str, .url = index->git.url },
      });
    }

    marker = (si_index_marker_t) { .version = si_index_marker_version, .format = SPN_INDEX_FORMAT_RELEASES };
    if (si_index_marker_save(spn.mem, &spn.roots, staged, marker, &err)) {
      return spn_err_emit(&spn, err);
    }
    if (spn_git_add(dir, sp_fs_join_path(mem, dir, sp_str_lit(si_index_marker_file)))) {
      return spn_err_emit(&spn, (spn_err_union_t) {
        .kind = SPN_ERR_GIT,
        .git.command = sp_str_lit("git add"),
      });
    }
  }
  if (marker.format == SPN_INDEX_FORMAT_MANIFESTS) {
    return spn_err_emit(&spn, (spn_err_union_t) {
      .kind = SPN_ERR_INDEX_PUBLISH_PROTOCOL,
      .index = { .name = index->name.str, .url = index->git.url },
    });
  }

  sp_str_t root = sp_fs_join_path(mem, dir, marker.path);
  si_index_place_t work = { .at = sp_path_from_str(root), .dir = root };
  spn_index_pkg_t* existing = SP_NULLPTR;
  if (si_index_jsonl_get_package(&work, spn.mem, spn.intern, rel->name, &existing, &err)) {
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

  sp_str_t records = sp_fs_join_path(mem, root, si_index_jsonl_path(mem, rel->name));
  sp_fs_create_parent_at(sp_path_from_str(records));
  sp_fs_append_at(sp_path_from_str(records), sp_fmt(mem, "{}\n", sp_fmt_str(si_index_release_to_json(mem, rel))).value, sp_sys_default_file_perms);

  if (spn_git_add(dir, records)) {
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
    spn_path_t staging = spn_path_join(scratch.mem, spn_path_parent((spn_path_t) { .sub = index->location.dir }), sp_str_lit("t"));
    spn_path_t staged = sp_zero;
    if (spn_path_stage_dir(scratch.mem, SP_NULLPTR, staging, sp_str_lit("pub"), &staged)) {
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

    result = publish_from(scratch.mem, index, staged.sub, rel, url, cancelled, &output);
    sp_fs_remove_dir_at(sp_path_from_str(staged.sub));
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

spn_err_t si_index_publish(spn_index_info_t* index, spn_index_release_t* rel, sp_atomic_s32_t* cancelled) {
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      if (!sp_str_empty(index->git.rev)) {
        return spn_err_emit(&spn, (spn_err_union_t) {
          .kind = SPN_ERR_INDEX_PINNED,
          .index = { .name = index->name.str, .url = index->git.url },
        });
      }

      spn_try(publish_git(index, rel, cancelled));
      si_index_tip_expire(index);
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

void si_index_locate(spn_index_info_t* index, sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t store) {
  spn_path_t dir = sp_zero;
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      dir = spn_path_join(mem, store, spn_git_db_key(mem, index->git.url));
      break;
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      dir = spn_path_join(mem, store, spn_git_db_key(mem, index->http.url));
      break;
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      dir = spn_path_canonicalize(mem, roots, index->dir.path);
      break;
    }
  }
  index->location.at = spn_path_at(roots, dir);
  index->location.dir = spn_path_str(roots, mem, dir);
}

spn_index_info_t* spn_index_find(sp_da(spn_index_info_t)* indexes, spn_index_id_t id) {
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
    if (!spn_index_find(indexes, user[it].id)) {
      sp_da_push(*indexes, user[it]);
    }
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

