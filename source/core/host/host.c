#include "spn/host.h"

#include "ctx/ctx.h"
#include "ctx/types.h"
#include "git/cache.h"
#include "index/index.h"
#include "index/tip.h"
#include "index/types.h"
#include "lock/lock.h"
#include "paths/paths.h"
#include "project/types.h"

static spn_lock_t held_lock(sp_mem_t mem, spn_ctx_t* ctx) {
  spn_lock_t held = spn_lock_new(mem);
  if (ctx->project) {
    spn_err_union_t unreadable = sp_zero;
    spn_lock_load(mem, ctx->intern, &ctx->roots, ctx->project->paths.lock, &held, &unreadable);
  }
  return held;
}

static spn_index_desc_t describe_index(sp_mem_t mem, spn_ctx_t* ctx, const spn_lock_t* held, spn_index_info_t* index) {
  spn_index_desc_t desc = {
    .name = sp_str_copy(mem, index->name.str),
    .kind = index->kind,
    .protocol = index->protocol,
    .source = sp_str_copy(mem, spn_index_source(index)),
  };

  spn_path_t dir = sp_zero;
  switch (index->protocol) {
    case SPN_INDEX_PROTOCOL_GIT: {
      sp_str_t rev = si_index_pin(index, held);
      if (sp_str_empty(rev)) {
        rev = si_index_tip_plan(mem, index).rev;
      }
      if (sp_str_empty(rev)) {
        return desc;
      }

      bool fetching = false;
      spn_git_checkout_t* checkout = spn_git_cache_checkout(&ctx->caches.git, (spn_git_checkout_id_t) { .url = index->git.url, .rev = rev }, &fetching);
      if (fetching) {
        return desc;
      }
      dir = checkout->path;
      break;
    }
    case SPN_INDEX_PROTOCOL_HTTP: {
      return desc;
    }
    case SPN_INDEX_PROTOCOL_DIR: {
      dir = (spn_path_t) { .sub = index->location.dir };
      break;
    }
  }

  si_index_marker_t marker = sp_zero;
  spn_err_union_t unread = sp_zero;
  if (si_index_read_marker(index, mem, ctx->intern, &ctx->roots, dir, &marker, &unread)) {
    return desc;
  }
  desc.format = marker.format;
  desc.location = spn_path_str(&ctx->roots, mem, dir);
  return desc;
}

sp_str_t spn_ctx_project_dir(spn_ctx_t* ctx) {
  return ctx->roots.dirs[SPN_PATH_ROOT_PROJECT];
}

spn_path_t spn_ctx_collapse_path(spn_ctx_t* ctx, sp_str_t path) {
  if (!sp_fs_is_absolute(path) || !spn_path_normal(path)) {
    return (spn_path_t) { .sub = path };
  }
  return spn_path_make(&ctx->roots, path);
}

spn_index_arr_t spn_get_indexes(sp_mem_t mem, spn_ctx_t* ctx) {
  spn_lock_t held = held_lock(mem, ctx);
  spn_index_arr_t indexes = {
    .items = sp_alloc_n(mem, spn_index_desc_t, sp_da_size(ctx->indexes)),
    .count = (u32)sp_da_size(ctx->indexes),
  };
  sp_da_for(ctx->indexes, it) {
    indexes.items[it] = describe_index(mem, ctx, &held, &ctx->indexes[it]);
  }
  return indexes;
}

bool spn_get_index(sp_mem_t mem, spn_ctx_t* ctx, sp_str_t name, spn_index_desc_t* index) {
  spn_index_info_t* info = spn_find_index(ctx, name);
  if (!info) {
    return false;
  }
  spn_lock_t held = held_lock(mem, ctx);
  *index = describe_index(mem, ctx, &held, info);
  return true;
}
