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
#include "graph/dag.h"
#include "op/types.h"
#include "unit/unit.h"

static spn_err_t on_configure(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, sp_mem_t mem, spn_dag_obs_set_t* obs) {
  spn_pkg_unit_t* unit = (spn_pkg_unit_t*)user_data;
  spn_wasm_script_t* script = &unit->wasm.configure;
  if (spn_wasm_script_open(script, unit)) {
    return SPN_ERR_DAG_ACTION;
  }
  if (!spn_wasm_script_exports(script, sp_str_lit("configure"))) {
    return SPN_OK;
  }
  if (spn_wasm_script_call(script, unit, sp_str_lit("configure"), SPN_ABI_KIND_CONFIG, unit)) {
    return SPN_ERR_DAG_ACTION;
  }
  return SPN_OK;
}

spn_err_t configure(spn_op_t* op) {
  spn_session_t* s = op->session;
  if (spn_wasm_init()) {
    return spn_err_emit(s->ctx, (spn_err_union_t) { .kind = SPN_ERR_WASM_INIT_FAILED });
  }

  spn_try(spn_units_add_packages(s));
  spn_try(spn_units_add_targets(s, SPN_UNIT_SCOPE_METAPROGRAM));

  spn_dag_build_t* dag = spn_dag_build_new(op);
  s->dag.configure = dag;
  spn_dag_t* g = dag->graph;

  sp_da_for(s->units.metaprogram->packages, it) {
    spn_target_unit_t* reactor = s->units.metaprogram->packages[it]->scripts.configure;
    if (!reactor) {
      continue;
    }
    spn_try(spn_dag_build_add_target(dag, reactor));
  }

  sp_da_for(s->plans, pt) {
    sp_da_for(s->plans[pt].build->packages, it) {
      spn_pkg_unit_t* unit = s->plans[pt].build->packages[it];
      spn_target_unit_t* reactor = unit->metaprogram ? unit->metaprogram->scripts.configure : SP_NULLPTR;
      if (!reactor) {
        continue;
      }
      spn_dag_target_ids_t* ids = sp_ht_getp(dag->ids.targets, reactor);
      sp_assert(ids);
      spn_dag_id_t action = spn_dag_add_action(g, (spn_dag_action_config_t) {
        .kind = SPN_DAG_ACTION_UNCACHEABLE,
        .execute = on_configure,
        .user_data = unit,
      });
      spn_dag_action_add_input(g, action, ids->output);
    }
  }

  return spn_dag_build_run(dag, spn_cpu_count());
}
