#include "spn/errors.h"
#include "unit/types.h"

#include "dag/node.h"
#include "external/wasm/wasm.h"
#include "graph/nodes/nodes.h"

spn_err_t si_on_configure(const si_dag_ctx_t* c, void* user_data) {
  si_configure_t* ctx = (si_configure_t*)user_data;
  spn_pkg_unit_t* unit = ctx->unit;
  spn_wasm_script_t script = sp_zero;
  if (spn_wasm_script_open(unit, si_dag_resolve_input(c, ctx->reactor), c->obs, &script)) {
    return SPN_ERR_DAG_ACTION;
  }
  spn_err_t err = SPN_OK;
  if (spn_wasm_script_exports(&script, sp_str_lit("configure"))) {
    spn_wasm_handle_t config = { .ptr = unit, .kind = SPN_ABI_KIND_CONFIG };
    err = spn_wasm_script_call(&script, sp_str_lit("configure"), &config, 1);
  }
  spn_wasm_script_close(&script);
  return err ? SPN_ERR_DAG_ACTION : SPN_OK;
}
