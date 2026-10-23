#include "sp.h"
#include "macro/macro.h"
#include "spn/errors.h"
#include "core/types.h"
#include "pkg/types.h"
#include "session/types.h"
#include "spn/core.h"
#include "unit/types.h"

#include "cpu/cpu.h"
#include "error/error.h"
#include "external/wasm/wasm.h"
#include "dag/node.h"
#include "graph/dag.h"
#include "op/types.h"
#include "unit/unit.h"
#include "session/session.h"

typedef struct {
  spn_pkg_unit_t* unit;
  spn_dag_id_t reactor;
} configure_t;

static spn_err_t on_configure(const si_dag_ctx_t* c, void* user_data) {
  configure_t* ctx = (configure_t*)user_data;
  spn_pkg_unit_t* unit = ctx->unit;
  spn_wasm_script_t script = sp_zero;
  if (spn_wasm_script_open(c->mem, unit, si_dag_resolve_input(c, ctx->reactor), c->obs, &script)) {
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

spn_err_t configure(spn_op_t* op) {
  spn_session_t* s = op->session;
  if (spn_wasm_init()) {
    return spn_err_emit(s->ctx, (spn_err_union_t) { .kind = SPN_ERR_WASM_INIT_FAILED });
  }

  spn_try(spn_units_add_packages(s));
  spn_try(spn_units_add_targets(s, SPN_UNIT_SCOPE_METAPROGRAM));

  spn_dag_build_t* dag = spn_dag_build_new(op);
  spn_dag_t* g = dag->graph;

  spn_try(spn_dag_build_add_build(dag, s->units.metaprogram));
  sp_da_for(s->units.metaprogram->packages, it) {
    spn_target_unit_t* reactor = s->units.metaprogram->packages[it]->scripts.configure;
    if (!reactor) {
      continue;
    }
    spn_dag_build_declare_target(dag, reactor);
  }
  sp_da_for(s->units.metaprogram->packages, it) {
    spn_target_unit_t* reactor = s->units.metaprogram->packages[it]->scripts.configure;
    if (!reactor) {
      continue;
    }
    spn_try(spn_dag_build_add_target(dag, reactor));
  }

  sp_da_for(s->plans.build, i) {
    sp_da_for(s->plans.build[i].build->packages, j) {
      spn_pkg_unit_t* unit = s->plans.build[i].build->packages[j];
      spn_target_unit_t* reactor = unit->metaprogram ? unit->metaprogram->scripts.configure : SP_NULLPTR;
      if (!reactor) {
        continue;
      }
      spn_dag_target_ids_t* ids = sp_ht_getp(dag->ids.targets, reactor);
      sp_assert(ids);
      configure_t* ctx = sp_alloc_type(dag->mem, configure_t);
      *ctx = (configure_t) {
        .unit = unit,
        .reactor = ids->output,
      };
      spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .kind = SPN_DAG_ACTION_UNCACHEABLE,
        .execute = on_configure,
        .user_data = ctx,
      });
      spn_dag_action_add_input(g, action, ids->output);
    }
  }

  return spn_dag_build_run(dag, spn_cpu_count());
}
