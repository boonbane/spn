#include "spn/host.h"

#include "ctx/types.h"
#include "error/error.h"
#include "intern/intern.h"
#include "model/sync.h"
#include "op/op.h"
#include "paths/paths.h"
#include "session/types.h"
#include "unit/types.h"

spn_err_t spn_op_source_path(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_ctx_t* ctx = session->ctx;
  spn_dep_kind_t kind = op->request.path.kind;
  sp_intern_str_t name = spn_intern(op->request.path.name);

  spn_try(spn_resolve_and_sync(op));

  const spn_resolved_pkg_t* pkg = SP_NULLPTR;
  if (kind == SPN_DEP_KIND_PACKAGE && name.id == session->root->name.id) {
    pkg = session->root;
  }
  sp_da_for_ptr(session->root->edges, edge) {
    if (edge->id.name == name.id && edge->kind == kind) {
      pkg = sp_ht_getp(session->resolve, edge->id);
      break;
    }
  }
  if (!pkg) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_PKG_ABSENT,
      .subject = name.str,
      .absent = { .table = kind },
    });
  }

  op->result.path.path = spn_path_str(&ctx->roots, op->mem, pkg->roots.source);
  return SPN_OK;
}

spn_err_t spn_op_tool_path(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_ctx_t* ctx = session->ctx;
  sp_intern_str_t name = spn_intern(op->request.path.name);

  si_da_for_ptr(session->plan.closures, closure) {
    si_target_id_t target = closure->exe.target->id.target;
    if (target.kind == SPN_TARGET_KIND_TOOL && target.name == name.id) {
      op->result.path.path = spn_path_str(&ctx->roots, op->mem, closure->exe.path);
      return SPN_OK;
    }
  }
  return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_TOOL_MISSING, .target = { .name = name.str } });
}

spn_op_t* spn_source_path(spn_session_t* session, spn_path_request_t request) {
  spn_op_t* op = spn_op_new(session->ctx, session, SPN_OP_SOURCE_PATH);
  op->request.path = (spn_path_request_t) { .name = sp_str_copy(op->mem, request.name), .kind = request.kind };
  spn_op_submit(op);
  return op;
}

spn_op_t* spn_tool_path(spn_session_t* session, spn_path_request_t request) {
  spn_op_t* op = spn_op_new(session->ctx, session, SPN_OP_TOOL_PATH);
  op->request.path = (spn_path_request_t) { .name = sp_str_copy(op->mem, request.name) };
  spn_op_submit(op);
  return op;
}
