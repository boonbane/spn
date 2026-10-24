#include "model/model.h"

#include "ctx/types.h"
#include "dag/dag.h"
#include "error/error.h"
#include "git/patch.h"
#include "op/types.h"
#include "paths/paths.h"
#include "project/types.h"
#include "session/types.h"
#include "unit/unit.h"

spn_err_t resolve(spn_op_t* op);
spn_err_t sync_packages(spn_op_t* op, bool* reresolve);
spn_err_t configure(spn_op_t* op);

spn_err_t spn_model_establish(spn_op_t* op) {
  spn_session_t* session = op->session;
  const spn_path_roots_t* roots = &session->ctx->roots;

  spn_err_t hashed = SPN_OK;
  si_da_for_ptr(session->project->patches, decl) {
    spn_patch_t patch = { .qualified = decl->qualified };
    si_da_for_value(decl->files, file) {
      si_da_push(session->mem, patch.set.files, spn_path_str(roots, session->mem, file));
    }
    u32 missing = 0;
    patch.set.hash = spn_git_patch_hash(patch.set.files, &missing);
    if (!patch.set.hash) {
      spn_err_t kind = spn_err_emit(session->ctx, (spn_err_union_t) {
        .kind = SPN_ERR_PATCH_FILE_MISSING,
        .patch = { .name = decl->qualified.str, .path = patch.set.files[missing] },
      });
      if (!hashed) {
        hashed = kind;
      }
      continue;
    }
    si_da_push(session->mem, session->patches, patch);
  }
  spn_try(hashed);

  bool reresolve = sp_zero;
  do {
    spn_try(resolve(op));
    spn_try(sync_packages(op, &reresolve));
  } while (reresolve);

  spn_err_t unused = SPN_OK;
  si_da_for_ptr(session->patches, patch) {
    bool used = false;
    sp_ht_for_kv(session->resolve, it) {
      if (it.key->qualified == patch->qualified.id) {
        used = true;
        break;
      }
    }
    if (used) {
      continue;
    }
    spn_err_t kind = spn_err_emit(session->ctx, (spn_err_union_t) {
      .kind = SPN_ERR_PATCH_UNUSED,
      .patch = { .name = patch->qualified.str },
    });
    if (!unused) {
      unused = kind;
    }
  }
  spn_try(unused);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  session->dag.files_path = spn_path_join(session->mem, session->paths.build, sp_str_lit(".spn/files"));
  sp_mem_end_scratch(s);
  spn_dag_file_cache_init(&session->dag.files, session->mem, &session->ctx->roots);
  spn_dag_file_cache_load(&session->dag.files, session->dag.files_path);

  spn_try(configure(op));
  return spn_units_add_targets(session, SPN_UNIT_SCOPE_TARGET);
}
