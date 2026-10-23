#include "sp.h"
#include "macro/macro.h"
#include "ctx/types.h"
#include "unit/types.h"
#include "target/types.h"

#include "dag/node.h"
#include "external/cc.h"
#include "event/event.h"
#include "intern/intern.h"
#include "graph/build.h"
#include "graph/nodes/nodes.h"
#include "paths/paths.h"
#include "str/str.h"
#include "triple/triple.h"
#include "unit/package.h"
#include "unit/unit.h"

spn_err_t si_on_embed(const si_dag_ctx_t* c, void* user_data) {
  si_embed_t* embed = (si_embed_t*)user_data;
  spn_target_unit_t* unit = embed->target;
  spn_target_eval_t* eval = unit->eval;
  const spn_path_roots_t* roots = c->roots;

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_EMBED_START,
    .pkg = unit->pkg->eval.manifest->name.str,
    .embed_start = { .target = eval->decl->name.str, .num_files = si_da_size(eval->embed) },
  });

  sp_tm_timer_t timer = sp_tm_start_timer();

  spn_cc_embed_ctx_t embedder = sp_zero;
  spn_cc_embed_ctx_init(&embedder, spn.mem, spn_os_to_native_object_format(unit->pkg->build->profile.os), unit->pkg->build->profile.arch);

  si_da_for(eval->embed, it) {
    spn_embed_t entry = eval->embed[it];
    spn_embed_types_t types = entry.types;

    if (sp_str_empty(types.data)) {
      types.data = sp_str_lit("unsigned char");
    }

    if (sp_str_empty(types.size)) {
      types.size = sp_str_lit("unsigned long long");
    }

    switch (entry.kind) {
      case SPN_EMBED_FILE: {
        si_dag_observe(c, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_FILE, .path = entry.path });
        sp_str_t content = sp_zero;
        if (sp_io_read_file_at(embedder.mem, spn_path_at(roots, entry.path), &content) != SP_OK) {
          spn_event_buffer_push(spn.events, (spn_event_t) {
            .kind = SPN_EVENT_EMBED_FAILED,
            .pkg = unit->pkg->eval.manifest->name.str,
            .embed_failed = { .target = eval->decl->name.str, .path = entry.path, .error = sp_str_lit("file not found") },
          });
          return SPN_ERR_DAG_ACTION;
        }

        sp_mem_buffer_t data = {
          .data = (u8*)(uintptr_t)content.data,
          .len = content.len,
          .capacity = content.len,
        };
        spn_cc_embed_ctx_add(&embedder, data, spn_cc_symbol_from_embedded_file(c->mem, entry.dest), entry.dest, types.data, types.size);
        break;
      }
      case SPN_EMBED_DIR: {
        spn_path_t root = entry.path;
        si_dag_observe(c, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_ENUMERATION, .path = root });
        sp_fs_it_t walk = sp_fs_it_new_at(c->mem, spn_path_at(roots, root), 0);
        while (sp_fs_it_walk(&walk)) {
          sp_str_t rel = walk.entry.rel;
          if (walk.entry.kind == SP_FS_KIND_DIR) {
            si_dag_observe(c, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_ENUMERATION, .path = spn_path_join(c->mem, root, rel) });
            continue;
          }
          if (!sp_fs_is_file_at(walk.at)) continue;
          spn_path_t file = spn_path_join(c->mem, root, rel);
          si_dag_observe(c, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_FILE, .path = file });
          sp_str_t content = sp_zero;
          if (sp_io_read_file_at(embedder.mem, walk.at, &content) != SP_OK) {
            spn_event_buffer_push(spn.events, (spn_event_t) {
              .kind = SPN_EVENT_EMBED_FAILED,
              .pkg = unit->pkg->eval.manifest->name.str,
              .embed_failed = { .target = eval->decl->name.str, .path = spn_path_copy(spn.events->mem, file), .error = sp_str_lit("file not found") },
            });
            sp_fs_it_deinit(&walk);
            return SPN_ERR_DAG_ACTION;
          }
          sp_mem_buffer_t entry_data = {
            .data = (u8*)(uintptr_t)content.data,
            .len = content.len,
            .capacity = content.len,
          };
          sp_str_t dest = sp_fs_join_path(c->mem, entry.dest, rel);
          spn_cc_embed_ctx_add(&embedder, entry_data, spn_cc_symbol_from_embedded_file(c->mem, dest), dest, types.data, types.size);
        }
        sp_fs_it_deinit(&walk);
        break;
      }
    }
  }

  spn_err_t write_err = spn_cc_embed_ctx_write(&embedder, spn_path_at(roots, si_dag_resolve_output(c, embed->object)), spn_path_at(roots, si_dag_resolve_output(c, embed->header)));
  spn_cc_embed_ctx_free(&embedder);
  if (write_err) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_EMBED_FAILED,
      .pkg = unit->pkg->eval.manifest->name.str,
      .embed_failed = { .target = eval->decl->name.str, .error = sp_str_lit("embed write failed") },
    });
    return SPN_ERR_DAG_ACTION;
  }

  u64 elapsed = sp_tm_read_timer(&timer);
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_EMBED_PASSED,
    .pkg = unit->pkg->eval.manifest->name.str,
    .embed_passed = {
      .target = eval->decl->name.str,
      .object_path = embed->object_path,
      .header_path = embed->header_path,
      .time = elapsed,
    },
  });

  return SPN_OK;
}
