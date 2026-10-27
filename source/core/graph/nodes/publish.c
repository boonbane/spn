#include "ctx/types.h"
#include "spn/errors.h"
#include "unit/types.h"

#include "core/core.h"
#include "dag/glob.h"
#include "dag/node.h"
#include "enum/enum.h"
#include "event/event.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "str/str.h"

static spn_err_t publish_source(sp_mem_t mem, const si_dag_ctx_t* c, spn_path_t root, spn_publish_t* publish) {
  const spn_path_roots_t* roots = c->roots;
  sp_path_t dir = spn_path_at(roots, spn_path_join(mem, root, publish->dest));
  switch (publish->source.kind) {
    case SPN_SOURCE_FILE: {
      si_dag_observe(c, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_FILE, .path = publish->source.path });
      return spn_fs_update_file(spn_path_at(roots, publish->source.path), sp_path_join(mem, dir, sp_fs_get_name(publish->source.path.sub)));
    }
    case SPN_SOURCE_GLOB: {
      spn_dag_glob_result_t glob = sp_zero;
      spn_err_t err = spn_dag_glob(mem, roots, publish->source.path, &glob);
      sp_da_for(glob.obs, it) {
        si_dag_observe(c, glob.obs[it]);
      }
      if (err) return err;
      if (sp_da_empty(glob.matches)) {
        return SPN_ERROR;
      }
      sp_da_for(glob.matches, it) {
        spn_try(spn_fs_update_file(spn_path_at(roots, glob.matches[it].path), sp_path_join(mem, dir, glob.matches[it].rel)));
      }
      return SPN_OK;
    }
  }
  sp_unreachable_return(SPN_ERROR);
}

static spn_err_t publish_entry(sp_mem_t mem, const si_dag_ctx_t* c, spn_pkg_unit_t* unit, spn_path_t root, spn_publish_t* publish) {
  if (publish_source(mem, c, root, publish)) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_NODE_FAILED,
      .pkg = unit->eval.manifest->name.str,
      .node_failed = {
        .path = spn_path_str(c->roots, spn.events->mem, publish->source.path),
        .message = sp_fmt(spn.events->mem, "could not be published to {}", sp_fmt_str(sp_fs_join_path(spn.events->mem, spn_publish_root_to_str(publish->root), publish->dest))).value,
      },
    });
    return SPN_ERR_DAG_ACTION;
  }
  return SPN_OK;
}

static spn_err_t publish_unit(sp_mem_t mem, const si_dag_ctx_t* c, si_publish_t* publish) {
  spn_pkg_unit_t* unit = publish->pkg;
  spn_path_t dirs [] = {
    [SPN_PUBLISH_ROOT_INCLUDE] = si_dag_resolve_output(c, publish->include),
    [SPN_PUBLISH_ROOT_SHARE] = si_dag_resolve_output(c, publish->share),
  };

  si_da_for(unit->eval.publish, it) {
    spn_publish_t* entry = &unit->eval.publish[it];
    spn_try(publish_entry(mem, c, unit, dirs[entry->root], entry));
  }
  si_om_for(unit->eval.targets, it) {
    si_target_t* target = si_om_at(unit->eval.targets, it);
    si_da_for(target->publish, jt) {
      spn_publish_t* entry = &target->publish[jt];
      spn_try(publish_entry(mem, c, unit, dirs[entry->root], entry));
    }
  }
  return SPN_OK;
}

spn_err_t si_on_publish_file(const si_dag_ctx_t* c, void* user_data) {
  si_publish_file_t* file = (si_publish_file_t*)user_data;
  sp_path_t source = spn_path_at(c->roots, si_dag_resolve_input(c, file->source));
  sp_path_t dest = spn_path_at(c->roots, si_dag_resolve_output(c, file->dest));
  if (sp_fs_copy_file_at(source, dest, SP_FS_ATOMIC_REPLACE)) {
    return SPN_ERR_DAG_OUTPUT_WRITE;
  }
  return SPN_OK;
}

spn_err_t si_on_publish(const si_dag_ctx_t* c, void* user_data) {
  sp_mem_arena_marker_t s = sp_mem_begin_scratch();
  spn_err_t err = publish_unit(s.mem, c, (si_publish_t*)user_data);
  sp_mem_end_scratch(s);
  return err;
}
