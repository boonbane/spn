#include "ctx/types.h"
#include "spn/errors.h"
#include "unit/types.h"

#include "dag/node.h"
#include "event/event.h"
#include "external/wasm/wasm.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "str/str.h"
#include "unit/package.h"

spn_err_t si_on_user_node(const si_dag_ctx_t* c, void* user_data) {
  si_user_node_t* ctx = (si_user_node_t*)user_data;
  spn_user_node_t* node = ctx->node;
  spn_pkg_unit_t* unit = node->unit;
  const spn_path_roots_t* roots = c->roots;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SCRIPT_USER_FN,
    .pkg = unit->eval.manifest->name.str,
    .script_user_fn = { .tag = node->tag }
  });

  si_da_for(node->outputs, it) {
    spn_user_output_t* out = &node->outputs[it];
    sp_path_t declared = spn_path_at(roots, out->path);
    sp_err_t err = SP_OK;
    switch (out->kind) {
      case SPN_DAG_ARTIFACT_KIND_FILE:  err = sp_fs_remove_file_at(declared); break;
      case SPN_DAG_ARTIFACT_KIND_TREE:  err = sp_fs_remove_dir_at(declared); break;
      case SPN_DAG_ARTIFACT_KIND_VALUE: sp_unreachable_case();
    }
    if (err && err != SP_ERR_SYS_NOT_FOUND) {
      spn_event_buffer_push(spn.events, (spn_event_t) {
        .kind = SPN_EVENT_NODE_FAILED,
        .pkg = unit->eval.manifest->name.str,
        .node_failed = {
          .path = spn_path_str(roots, spn.events->mem, out->path),
          .message = sp_fmt(spn.events->mem, "could not be removed before node {} ran", sp_fmt_str(node->tag)).value,
        },
      });
      return SPN_ERR_DAG_ACTION;
    }
  }

  if (!sp_str_empty(node->fn)) {
    spn_wasm_script_t script = sp_zero;
    if (spn_wasm_script_open(unit, si_dag_resolve_input(c, ctx->script), c->obs, &script)) {
      return SPN_ERR_DAG_ACTION;
    }
    spn_err_t err = spn_wasm_script_call(&script, node->fn, SP_NULLPTR, 0);
    spn_wasm_script_close(&script);
    if (err) {
      return SPN_ERR_DAG_ACTION;
    }
  }

  si_da_for(node->outputs, it) {
    spn_user_output_t* out = &node->outputs[it];
    sp_path_t target = spn_path_at(roots, si_dag_resolve_output(c, c->produces[it]));
    sp_path_t declared = spn_path_at(roots, out->path);
    sp_err_t err = SP_OK;
    if (out->stamp) {
      sp_fs_create_file_at(target);
    }
    else {
      switch (out->kind) {
        case SPN_DAG_ARTIFACT_KIND_FILE:  err = sp_fs_copy_file_at(declared, target, SP_FS_ATOMIC_REPLACE); break;
        case SPN_DAG_ARTIFACT_KIND_TREE:  err = sp_fs_copy_tree_at(declared, target, SP_FS_ATOMIC_REPLACE); break;
        case SPN_DAG_ARTIFACT_KIND_VALUE: sp_unreachable_case();
      }
    }
    if (err) {
      spn_event_buffer_push(spn.events, (spn_event_t) {
        .kind = SPN_EVENT_NODE_FAILED,
        .pkg = unit->eval.manifest->name.str,
        .node_failed = {
          .path = spn_path_str(roots, spn.events->mem, out->path),
          .message = err == SP_ERR_SYS_NOT_FOUND
            ? sp_fmt(spn.events->mem, "was declared as an output of node {} but was not produced", sp_fmt_str(node->tag)).value
            : sp_fmt(spn.events->mem, "output of node {} could not be copied into the build", sp_fmt_str(node->tag)).value,
        },
      });
      return SPN_ERR_DAG_ACTION;
    }
  }

  return SPN_OK;
}
