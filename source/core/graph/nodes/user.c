#include "ctx/types.h"
#include "spn/errors.h"
#include "unit/types.h"

#include "dag/dag.h"
#include "event/event.h"
#include "external/wasm/wasm.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "str/str.h"
#include "unit/package.h"

spn_err_t on_user_node(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_user_node_t* node = (spn_user_node_t*)user_data;
  spn_pkg_unit_t* pkg = node->pkg;

  spn_pkg_unit_announce_compile(pkg);

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SCRIPT_USER_FN,
    .pkg = pkg->info->name,
    .script_user_fn = { .tag = node->tag }
  });

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_wasm_output_t* harvest = sp_alloc_n(s.mem, spn_wasm_output_t, sp_da_size(node->outputs));
  u32 num_harvest = 0;
  sp_da_for(node->outputs, it) {
    if (node->outputs[it].stamp) {
      sp_fs_create_file_at(spn_path_at(g->roots, outputs[it]));
      continue;
    }
    harvest[num_harvest++] = (spn_wasm_output_t) { .declared = &node->outputs[it], .to = outputs[it] };
  }

  spn_err_t err = SPN_OK;
  if (!sp_str_empty(node->fn) && spn_wasm_call_export_ex(pkg, node->fn, SPN_ABI_KIND_NONE, SP_NULLPTR, obs, harvest, num_harvest)) {
    err = SPN_ERR_DAG_ACTION;
  }

  sp_for(it, num_harvest) {
    if (!harvest[it].err) {
      continue;
    }
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_NODE_FAILED,
      .pkg = pkg->info->name,
      .node_failed = {
        .path = spn_path_str(g->roots, spn.mem, harvest[it].declared->path),
        .message = harvest[it].err == SP_ERR_SYS_NOT_FOUND
          ? sp_fmt(spn.mem, "was declared as an output of node {} but was not produced", sp_fmt_str(node->tag)).value
          : sp_fmt(spn.mem, "output of node {} could not be copied into the build", sp_fmt_str(node->tag)).value,
      },
    });
    err = SPN_ERR_DAG_ACTION;
    break;
  }

  sp_mem_end_scratch(s);
  return err;
}
