#include "sp.h"
#include "macro/macro.h"
#include "spn.h"

#include "api/api.h"
#include "api/types.h"
#include "ctx/types.h"
#include "event/types.h"
#include "unit/types.h"

#include "event/event.h"
#include "external/wasm/wasi.h"
#include "external/wasm/wasm.h"
#include "paths/paths.h"
#include "unit/unit.h"
#include "intern/intern.h"

spn_node_t* spn_add_node(spn_config_t* config, const c8* tag) {
  spn_pkg_unit_t* unit = spn_api_unit(config);
  if (sp_str_contains(sp_cstr_as_str(tag), sp_str_lit("/"))) {
    return SP_NULLPTR;
  }

  sp_mem_t mem = spn.mem;
  u32 index = sp_da_size(unit->user_nodes);
  spn_user_node_t node = {
    .unit = unit,
    .tag = spn_intern_cstr(tag).str,
  };
  sp_da_push(unit->user_nodes, node);

  spn_node_t* out = sp_alloc_type(mem, spn_node_t);
  *out = (spn_node_t) {
    .ref = {
      .pkg = unit,
      .index = index,
    },
  };

  return out;
}

static spn_err_t find_output(spn_pkg_unit_t* unit, sp_mem_t mem, spn_root_t root, sp_str_t sub, spn_tree_t* tree) {
  spn_wasm_mounts_t mounts = spn_wasm_mounts(unit);
  const spn_wasi_mount_t* mount = SP_NULLPTR;
  spn_try(spn_wasi_find_mount(mounts.dirs, sp_carr_len(mounts.dirs), root, sub, &mount));
  if (!mount->writable) {
    return SPN_ERR_OUTPUT_OUTSIDE;
  }
  spn_path_t host = spn_path_join(mem, mount->host, sub);
  spn_path_t dirs [] = { unit->paths.work, unit->paths.lib, unit->paths.bin, unit->paths.vendor };
  sp_carr_for(dirs, it) {
    spn_path_rel_t rel = spn_path_within(dirs[it], host);
    if (!rel.within) {
      continue;
    }
    if (sp_str_empty(rel.sub)) {
      return SPN_ERR_OUTPUT_IS_ROOT;
    }
    if (sp_fs_is_absolute_for(rel.sub, SP_FS_PATH_WINDOWS)) {
      return SPN_ERR_PATH_NOT_NORMAL;
    }
    *tree = mount->tree;
    return SPN_OK;
  }
  return SPN_ERR_OUTPUT_OUTSIDE;
}

static spn_err_t add_output(spn_node_t* node, spn_root_t root, sp_str_t sub, spn_dag_artifact_kind_t kind) {
  spn_user_node_t* info = spn_node_deref(node->ref);
  spn_pkg_unit_t* unit = node->ref.pkg;
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_tree_t tree = SPN_TREE_NONE;
  spn_err_t err = find_output(unit, scratch.mem, root, sub, &tree);
  if (!err) {
    si_da_push(unit->units->mem, info->outputs, ((spn_user_output_t) {
      .tree = tree,
      .sub = sp_str_copy(spn.mem, sub),
      .kind = kind,
      .path = spn_path_join(spn.mem, spn_tree_root(unit->paths.roots, tree), sub),
    }));
  }
  sp_mem_end_scratch(scratch);
  return err;
}

spn_err_t spn_node_add_output(spn_node_t* node, spn_root_t root, const c8* sub) {
  return add_output(node, root, sp_cstr_as_str(sub), SPN_DAG_ARTIFACT_KIND_FILE);
}

spn_err_t spn_node_add_output_dir(spn_node_t* node, spn_root_t root, const c8* sub) {
  return add_output(node, root, sp_cstr_as_str(sub), SPN_DAG_ARTIFACT_KIND_TREE);
}

spn_err_t spn_node_link(spn_node_t* from, spn_node_t* to) {
  if (si_da_empty(spn_node_deref(from->ref)->outputs)) {
    return SPN_ERR_NODE_NO_OUTPUTS;
  }
  spn_user_node_t* info = spn_node_deref(to->ref);
  si_da_push(to->ref.pkg->units->mem, info->deps, from->ref);
  return SPN_OK;
}

spn_err_t spn_node_set_fn(spn_node_t* node, const c8* fn) {
  if (!node->ref.pkg->metaprogram) {
    return SPN_ERR_NODE_NO_SCRIPT;
  }
  spn_node_deref(node->ref)->fn = spn_intern_cstr(fn).str;
  return SPN_OK;
}
