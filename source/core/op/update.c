#include "spn/host.h"

#include "ctx/types.h"
#include "error/error.h"
#include "event/event.h"
#include "index/types.h"
#include "intern/intern.h"
#include "lock/lock.h"
#include "model/sync.h"
#include "op/op.h"
#include "project/types.h"
#include "semver/convert.h"
#include "session/types.h"

static spn_err_t update(sp_mem_t scratch, spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_ctx_t* ctx = session->ctx;
  spn_str_arr_t named = op->request.update.names;

  spn_lock_t held = sp_zero;
  spn_err_union_t unreadable = sp_zero;
  if (spn_lock_load(session->mem, ctx->intern, &ctx->roots, session->project->paths.lock, &held, &unreadable)) {
    return spn_err_emit(ctx, unreadable);
  }

  spn_lock_t kept = spn_lock_new(scratch);
  if (named.count) {
    sp_da(sp_intern_id_t) names = sp_da_new(scratch, sp_intern_id_t);
    sp_for(it, named.count) {
      sp_intern_str_t name = spn_intern(named.items[it]);
      bool locked = false;
      sp_da_for(held.entries, et) {
        locked = locked || held.entries[et].name.id == name.id;
      }
      if (!locked) {
        return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_LOCK_ABSENT, .subject = name.str });
      }
      sp_da_push(names, name.id);
    }

    sp_da_for(held.entries, it) {
      bool updated = false;
      sp_da_for(names, nt) {
        updated = updated || names[nt] == held.entries[it].name.id;
      }
      if (!updated) {
        spn_lock_insert(&kept, held.entries[it]);
      }
    }
    sp_da_for(held.indexes, it) {
      spn_lock_pin_index(&kept, held.indexes[it]);
    }
  }

  sp_da(spn_index_info_t*) indexes = sp_da_new(scratch, spn_index_info_t*);
  sp_da_for(ctx->indexes, it) {
    sp_da_push(indexes, &ctx->indexes[it]);
  }
  spn_try(si_read_index_tips(op, indexes));

  spn_lock_t lock = sp_zero;
  spn_try(si_resolve_and_sync_pins(op, &held, &kept, &lock));

  spn_lock_diff_t diff = spn_lock_diff(&held, &lock);
  while (spn_lock_diff_next(&diff)) {
    const spn_lock_entry_t* entry = diff.now ? diff.now : diff.was;
    spn_event_buffer_push(ctx->events, (spn_event_t) {
      .kind = diff.now ? SPN_EVENT_LOCK_ADDED : SPN_EVENT_LOCK_REMOVED,
      .pkg = entry->name.str,
      .resolve_pkg = {
        .name = entry->name.str,
        .version = spn_semver_to_str(ctx->events->mem, entry->version),
      },
    });
  }
  return SPN_OK;
}

spn_err_t spn_op_update(spn_op_t* op) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_err_t err = update(scratch.mem, op);
  sp_mem_end_scratch(scratch);
  return err;
}

spn_op_t* spn_update(spn_session_t* session, spn_update_request_t request) {
  spn_op_t* op = spn_op_new(session->ctx, session, SPN_OP_UPDATE);
  op->request.update.names = (spn_str_arr_t) {
    .items = sp_alloc_n(op->mem, sp_str_t, request.names.count),
    .count = request.names.count,
  };
  sp_for(it, request.names.count) {
    op->request.update.names.items[it] = sp_str_copy(op->mem, request.names.items[it]);
  }
  spn_op_submit(op);
  return op;
}
