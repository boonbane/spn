#include "ctx/types.h"
#include "spn/errors.h"
#include "unit/types.h"

#include "dag/node.h"
#include "event/event.h"
#include "external/wasm/wasi.h"
#include "external/wasm/wasm.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"

static spn_err_t fail(spn_user_node_t* node, spn_node_failure_t reason, spn_path_t path) {
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_NODE_FAILED,
    .pkg = node->unit->pkg.manifest->name.str,
    .node_failed = { .tag = node->tag, .reason = reason, .path = path },
  });
  return SPN_ERR_DAG_ACTION;
}

spn_err_t si_on_user_node(const si_dag_ctx_t* c, void* user_data) {
  si_user_node_t* ctx = (si_user_node_t*)user_data;
  spn_user_node_t* node = ctx->node;
  spn_pkg_unit_t* unit = node->unit;
  const spn_path_roots_t* roots = c->roots;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_SCRIPT_USER_FN,
    .pkg = unit->pkg.manifest->name.str,
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
      return fail(node, SPN_NODE_FAILURE_REMOVE, out->path);
    }
    if (sp_fs_create_parent_at(declared)) {
      return fail(node, SPN_NODE_FAILURE_CREATE, spn_path_parent(out->path));
    }
  }

  if (ctx->script.occupied) {
    spn_wasm_script_t script = sp_zero;
    if (spn_wasm_script_open(unit, si_dag_resolve_input(c, ctx->script), c->obs, &script)) {
      return SPN_ERR_DAG_ACTION;
    }
    spn_err_t err = spn_wasm_script_call(&script, node->fn, SP_NULLPTR, 0);
    spn_path_t stray = sp_zero;
    if (!err && spn_wasi_stray_write(script.wasi, &stray)) {
      err = fail(node, SPN_NODE_FAILURE_STRAY, spn_path_copy(spn.events->mem, stray));
    }
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
    switch (out->kind) {
      case SPN_DAG_ARTIFACT_KIND_FILE:  err = sp_fs_copy_file_at(declared, target, SP_FS_ATOMIC_REPLACE); break;
      case SPN_DAG_ARTIFACT_KIND_TREE:  err = sp_fs_copy_tree_at(declared, target, SP_FS_ATOMIC_REPLACE); break;
      case SPN_DAG_ARTIFACT_KIND_VALUE: sp_unreachable_case();
    }
    if (err) {
      return fail(node, err == SP_ERR_SYS_NOT_FOUND ? SPN_NODE_FAILURE_UNPRODUCED : SPN_NODE_FAILURE_COPY, out->path);
    }
  }

  return SPN_OK;
}
