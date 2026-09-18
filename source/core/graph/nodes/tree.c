#include "ctx/types.h"
#include "spn/errors.h"
#include "unit/types.h"

#include "core/core.h"
#include "dag/dag.h"
#include "enum/enum.h"
#include "event/event.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "str/str.h"
#include "unit/package.h"

static spn_err_t publish_copy(sp_mem_t scratch, spn_tree_roots_t trees, sp_str_t root, spn_publish_copy_t* copy, spn_dag_obs_set_t* obs) {
  spn_path_t pattern = spn_path_join(scratch, spn_tree_root(trees, copy->tree), copy->pattern);
  spn_dag_glob_result_t glob = sp_zero;
  spn_try(spn_dag_glob(scratch, &spn.roots, pattern, &glob));
  sp_da_for(glob.obs, it) {
    spn_dag_observe(obs, glob.obs[it]);
  }
  if (sp_da_empty(glob.matches)) {
    return SPN_ERROR;
  }

  sp_str_t dir = sp_fs_join_path(scratch, root, copy->dest);
  sp_str_buf_t buf = sp_zero;
  sp_da_for(glob.matches, it) {
    spn_try(spn_fs_update_file(
      spn_path_str(&spn.roots, sp_str_buf_as_mem(&buf), glob.matches[it].path),
      sp_fs_join_path(scratch, dir, glob.matches[it].rel)
    ));
  }
  return SPN_OK;
}

static spn_err_t publish_tree(sp_mem_t scratch, spn_dag_t* g, spn_pkg_unit_t* unit, spn_path_t include, spn_path_t stamp, spn_dag_obs_set_t* obs) {
  sp_str_t root = spn_path_str(g->roots, scratch, include);
  if (spn_pkg_unit_publish_headers(unit, root)) {
    return SPN_ERR_DAG_ACTION;
  }

  sp_da_for(unit->info->publish.copy, it) {
    spn_publish_copy_t* copy = &unit->info->publish.copy[it];
    if (publish_copy(scratch, unit->paths.roots, root, copy, obs)) {
      spn_event_buffer_push(spn.events, (spn_event_t) {
        .kind = SPN_EVENT_NODE_FAILED,
        .pkg = unit->info->name,
        .node_failed = {
          .path = sp_fs_join_path(spn.mem, spn_tree_to_str(copy->tree), copy->pattern),
          .message = sp_fmt(spn.mem, "could not be published to {}", sp_fmt_str(sp_fs_join_path(spn.mem, sp_str_lit("include"), copy->dest))).value,
        },
      });
      return SPN_ERR_DAG_ACTION;
    }
  }

  sp_fs_create_file(spn_path_str(g->roots, scratch, stamp));
  return SPN_OK;
}

spn_err_t on_publish_tree(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = publish_tree(s.mem, g, (spn_pkg_unit_t*)user_data, outputs[0], outputs[1], obs);
  sp_mem_end_scratch(s);
  return err;
}
