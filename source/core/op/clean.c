#include "ctx/types.h"
#include "error/error.h"
#include "session/types.h"

#include "spn/host.h"
#include "ctx/ctx.h"
#include "op/op.h"
#include "paths/paths.h"
#include "profile/profile.h"
#include "stage/stage.h"
#include "os/os.h"

static spn_err_t remove_path(spn_ctx_t* ctx, spn_path_t path) {
  if (sp_fs_remove(spn_path_at(&ctx->roots, path)) != SP_OK) {
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_REMOVE, .fs = { .path = path } });
  }

  return SPN_OK;
}

spn_err_t spn_op_clean(spn_op_t* op) {
  spn_ctx_t* ctx = op->ctx;
  spn_try(spn_ctx_require_project(ctx));
  spn_path_t project = spn_path_from_root(SPN_PATH_ROOT_PROJECT);

  sp_str_t staged = sp_zero;
  sp_io_read_file_at(ctx->heap, spn_path_at(&ctx->roots, spn_path_join(ctx->heap, project, sp_str_lit("build/.spn/staged"))), &staged);
  spn_stage_for(staged, it) {
    spn_path_t path = { .root = project.root, .sub = it.record.path };
    if (spn_stage_remove(spn_path_at(&ctx->roots, path))) {
      return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_REMOVE, .fs = { .path = path } });
    }
  }

  spn_try(remove_path(ctx, spn_path_join(ctx->heap, project, sp_str_lit("compile_commands.json"))));
  return remove_path(ctx, spn_path_join(ctx->heap, project, sp_str_lit("build")));
}

spn_op_t* spn_clean(spn_ctx_t* ctx) {
  spn_op_t* op = spn_op_new(ctx, SP_NULLPTR, SPN_OP_CLEAN);
  spn_op_submit(op);
  return op;
}

spn_err_t spn_op_clean_profile(spn_op_t* op) {
  spn_session_t* session = op->session;
  spn_path_t dir = spn_path_join(session->mem, session->paths.build, spn_profile_build_dir(session->mem, &session->profile));
  return remove_path(session->ctx, dir);
}

spn_op_t* spn_clean_profile(spn_session_t* session) {
  spn_op_t* op = spn_op_new(session->ctx, session, SPN_OP_CLEAN_PROFILE);
  spn_op_submit(op);
  return op;
}
