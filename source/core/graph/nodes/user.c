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

spn_err_t spn_dag_exec_user(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_user_node_t* node = (spn_user_node_t*)user_data;
  spn_pkg_unit_t* pkg = node->pkg;

  spn_pkg_unit_announce_compile(pkg);

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SCRIPT_USER_FN,
    .pkg = pkg->info->name,
    .script_user_fn = { .tag = node->tag }
  });

  sp_str_buf_t declared_buf = sp_zero;
  sp_da_for(action->produces, it) {
    spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    sp_str_t declared = spn_path_str(g->roots, sp_str_buf_as_mem(&declared_buf), artifact->path);
    sp_err_t err = SP_OK;
    switch (artifact->kind) {
      case SPN_DAG_ARTIFACT_KIND_FILE:  err = sp_fs_remove_file(declared); break;
      case SPN_DAG_ARTIFACT_KIND_TREE:  err = sp_fs_remove_dir(declared); break;
      case SPN_DAG_ARTIFACT_KIND_VALUE: sp_unreachable_case();
    }
    if (err && err != SP_ERR_SYS_NOT_FOUND) {
      spn_event_buffer_push(spn.events, (spn_event_t) {
        .kind = SPN_EVENT_NODE_FAILED,
        .pkg = pkg->info->name,
        .node_failed = {
          .path = spn_path_str(g->roots, spn.mem, artifact->path),
          .message = sp_fmt(spn.mem, "could not be removed before node {} ran", sp_fmt_str(node->tag)).value,
        },
      });
      return SPN_ERR_DAG_ACTION;
    }
  }

  if (!sp_str_empty(node->fn)) {
    if (spn_wasm_call_export_ex(pkg, node->fn, SPN_ABI_KIND_NONE, SP_NULLPTR, obs)) {
      return SPN_ERR_DAG_ACTION;
    }
  }

  sp_str_buf_t target_buf = sp_zero;
  sp_da_for(action->produces, it) {
    spn_dag_artifact_t* artifact = spn_dag_find_artifact(g, action->produces[it]);
    sp_str_t target = spn_path_str(g->roots, sp_str_buf_as_mem(&target_buf), outputs[it]);
    sp_str_t declared = spn_path_str(g->roots, sp_str_buf_as_mem(&declared_buf), artifact->path);
    sp_err_t err = SP_OK;
    if (node->outputs[it].stamp) {
      sp_fs_create_file(target);
    }
    else {
      switch (artifact->kind) {
        case SPN_DAG_ARTIFACT_KIND_FILE:  err = sp_fs_copy_file(declared, target, SP_FS_ATOMIC_REPLACE); break;
        case SPN_DAG_ARTIFACT_KIND_TREE:  err = sp_fs_copy_tree(declared, target, SP_FS_ATOMIC_REPLACE); break;
        case SPN_DAG_ARTIFACT_KIND_VALUE: sp_unreachable_case();
      }
    }
    if (err) {
      spn_event_buffer_push(spn.events, (spn_event_t) {
        .kind = SPN_EVENT_NODE_FAILED,
        .pkg = pkg->info->name,
        .node_failed = {
          .path = spn_path_str(g->roots, spn.mem, artifact->path),
          .message = err == SP_ERR_SYS_NOT_FOUND
            ? sp_fmt(spn.mem, "was declared as an output of node {} but was not produced", sp_fmt_str(node->tag)).value
            : sp_fmt(spn.mem, "output of node {} could not be copied into the build", sp_fmt_str(node->tag)).value,
        },
      });
      return SPN_ERR_DAG_ACTION;
    }
  }

  return SPN_OK;
}
