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

static spn_err_t publish_entry(spn_dag_t* g, spn_pkg_unit_t* unit, spn_path_t root, spn_publish_t* publish, spn_dag_obs_set_t* obs) {
  const spn_path_roots_t* roots = g->roots;
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  sp_path_t dir = spn_path_at(roots, spn_path_join(s.mem, root, publish->dest));
  spn_err_t err = SPN_OK;
  switch (publish->source.kind) {
    case SPN_SOURCE_FILE: {
      spn_dag_observe(obs, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_FILE, .path = publish->source.path });
      err = spn_fs_update_file(spn_path_at(roots, publish->source.path), sp_path_join(s.mem, dir, sp_fs_get_name(publish->source.path.sub)));
      break;
    }
    case SPN_SOURCE_GLOB: {
      spn_dag_glob_result_t glob = sp_zero;
      err = spn_dag_glob(s.mem, roots, publish->source.path, &glob);
      sp_da_for(glob.obs, it) {
        spn_dag_observe(obs, glob.obs[it]);
      }
      if (!err && sp_da_empty(glob.matches)) {
        err = SPN_ERROR;
      }
      sp_da_for(glob.matches, it) {
        if (err) {
          break;
        }
        err = spn_fs_update_file(spn_path_at(roots, glob.matches[it].path), sp_path_join(s.mem, dir, glob.matches[it].rel));
      }
      break;
    }
  }
  sp_mem_end_scratch(s);

  if (err) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_NODE_FAILED,
      .pkg = unit->info->name,
      .node_failed = {
        .path = spn_path_str(roots, spn.mem, publish->source.path),
        .message = sp_fmt(spn.mem, "could not be published to {}", sp_fmt_str(sp_fs_join_path(spn.mem, spn_publish_root_to_str(publish->root), publish->dest))).value,
      },
    });
    return SPN_ERR_DAG_ACTION;
  }
  return SPN_OK;
}

spn_err_t si_on_publish(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_pkg_unit_t* unit = (spn_pkg_unit_t*)user_data;
  spn_path_t dirs [] = {
    [SPN_PUBLISH_ROOT_INCLUDE] = outputs[0],
    [SPN_PUBLISH_ROOT_SHARE] = outputs[1],
  };

  si_da_for(unit->info->publish, it) {
    spn_publish_t* entry = &unit->info->publish[it];
    spn_try(publish_entry(g, unit, dirs[entry->root], entry, obs));
  }
  si_om_for(unit->info->targets, it) {
    spn_target_info_t* target = si_om_at(unit->info->targets, it);
    si_da_for(target->publish, jt) {
      spn_publish_t* entry = &target->publish[jt];
      spn_try(publish_entry(g, unit, dirs[entry->root], entry, obs));
    }
  }

  sp_fs_create_file_at(spn_path_at(g->roots, outputs[2]));
  return SPN_OK;
}
