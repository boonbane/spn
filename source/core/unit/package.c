#include "unit/package.h"

#include "core/core.h"
#include "ctx/ctx.h"
#include "sp.h"
#include "spn/core.h"
#include "event/event.h"
#include "external/cc.h"
#include "external/git.h"
#include "external/tom.h"
#include "paths/paths.h"
#include "pkg/pkg.h"
#include "semver/convert.h"
#include "session/session.h"
#include "unit/types.h"
#include "unit/unit.h"

spn_user_output_t spn_pkg_unit_node_stamp(spn_pkg_unit_t* ctx, spn_user_node_t* node) {
  return (spn_user_output_t) {
    .dir = SPN_DIR_WORK,
    .sub = sp_fs_join_path(spn.mem, sp_str_lit("stamp"), node->tag),
    .kind = SPN_DAG_ARTIFACT_KIND_FILE,
    .path = spn_path_join(spn.mem, ctx->paths.stamp, node->tag),
    .stamp = true,
  };
}

typedef sp_ht(spn_path_t, spn_path_t) staged_header_set_t;

static spn_err_t header_collision(spn_pkg_unit_t* unit, sp_str_t path, spn_path_t first, spn_path_t second) {
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_ERR,
    .pkg = unit->info->name,
    .err = {
      .kind = SPN_ERR_HEADER_COLLISION,
      .header_collision = {
        .path = path,
        .first = spn_path_str(&unit->session->ctx->roots, spn.mem, first),
        .second = spn_path_str(&unit->session->ctx->roots, spn.mem, second),
      },
    },
  });
  return SPN_ERROR;
}

static spn_err_t header_copy_failed(spn_pkg_unit_t* unit, sp_str_t path) {
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_NODE_FAILED,
    .pkg = unit->info->name,
    .node_failed = {
      .path = path,
      .message = sp_str_lit("could not be published to the package store"),
    },
  });
  return SPN_ERROR;
}

typedef struct {
  spn_path_t from;
  spn_path_t to;
  sp_str_t name;
} staged_header_t;

bool spn_pkg_unit_header_it_valid(const spn_pkg_unit_header_it_t* it) {
  return it->map < it->count;
}

void spn_pkg_unit_header_it_next(spn_pkg_unit_header_it_t* it) {
  for (; it->map < it->count; it->map++, it->target = 0) {
    spn_target_map_t targets = it->maps[it->map];
    for (; it->target < sp_om_size(targets); it->target++, it->index = 0) {
      spn_target_info_t* target = sp_str_om_at(targets, it->target);
      if (it->index < sp_da_size(target->headers)) {
        it->header = target->headers[it->index++];
        return;
      }
    }
  }
}

spn_pkg_unit_header_it_t spn_pkg_unit_header_it_begin(spn_pkg_unit_t* unit) {
  spn_pkg_unit_header_it_t it = {
    .maps = { unit->info->libs, unit->info->exes, unit->info->scripts, unit->info->tests },
    .count = unit->source == SPN_PKG_SOURCE_ROOT ? 4 : 1,
  };
  spn_pkg_unit_header_it_next(&it);
  return it;
}

spn_err_t spn_pkg_unit_publish_headers(spn_pkg_unit_t* unit, spn_path_t root) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  sp_da(staged_header_t) staged = sp_da_new(scratch.mem, staged_header_t);
  staged_header_set_t seen;
  sp_ht_init(scratch.mem, seen);
  sp_ht_set_fns(seen, spn_path_on_hash, spn_path_on_compare);

  spn_err_t err = SPN_OK;
  spn_pkg_unit_for_header(unit, it) {
    sp_str_t sub = spn_tree_rel(unit->paths.roots, it.header).sub;
    spn_path_t to = spn_path_join(scratch.mem, root, sub);

    spn_path_t* first = sp_ht_getp(seen, to);
    if (first) {
      if (!spn_path_equal(*first, it.header)) {
        err = header_collision(unit, sub, *first, it.header);
        break;
      }
      continue;
    }
    sp_ht_insert(seen, to, it.header);
    sp_da_push(staged, ((staged_header_t) { .from = it.header, .to = to, .name = sub }));
  }
  sp_da_for(staged, it) {
    if (err) {
      break;
    }
    if (spn_fs_update_file(spn_path_at(&unit->session->ctx->roots, staged[it].from), spn_path_at(&unit->session->ctx->roots, staged[it].to))) {
      err = header_copy_failed(unit, staged[it].name);
    }
  }
  sp_mem_end_scratch(scratch);
  return err;
}

// @spader I think this is wrong; it's called in four places and deduplicated with an atomic,
// but really we just want to add one graph node to log before anything in a package is compiled.
// I think of of the existing nodes would even suffice for this.
void spn_pkg_unit_announce_compile(spn_pkg_unit_t* unit) {
  if (!sp_atomic_s32_cas(&unit->compile_announced, 0, 1, SP_ATOMIC_SEQ_CST)) return;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_COMPILE_START,
    .pkg = unit->info->name,
    .compile_start = {
      .version = spn_semver_to_str(spn.mem, unit->info->version),
    },
  });
}
