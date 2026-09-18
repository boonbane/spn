#include "model/model.h"

#include "ctx/types.h"
#include "dag/dag.h"
#include "op/types.h"
#include "paths/paths.h"
#include "session/types.h"
#include "unit/unit.h"

spn_err_t resolve(spn_op_t* op);
spn_err_t sync_packages(spn_op_t* op, bool* reresolve);
spn_err_t configure(spn_op_t* op);

spn_err_t spn_model_establish(spn_op_t* op) {
  spn_session_t* session = op->session;
  bool reresolve = sp_zero;
  do {
    spn_try(resolve(op));
    spn_try(sync_packages(op, &reresolve));
  } while (reresolve);

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  session->dag.files_path = spn_path_str(&session->ctx->roots, session->mem, spn_path_join(s.mem, session->paths.build, sp_str_lit(".spn/files")));
  sp_mem_end_scratch(s);
  spn_dag_file_cache_init(&session->dag.files, session->mem, &session->ctx->roots);
  spn_dag_file_cache_load(&session->dag.files, session->dag.files_path);

  spn_try(configure(op));
  return spn_units_add_targets(session, SPN_UNIT_SCOPE_TARGET);
}
