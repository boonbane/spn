#include "sp.h"
#include "macro/macro.h"
#include "ctx/types.h"
#include "unit/types.h"
#include "target/types.h"

#include "dag/dag.h"
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

spn_err_t on_build_embedding(spn_dag_t* g, spn_dag_action_t* action, void* user_data, spn_dag_env_t* env, const spn_path_t* outputs, spn_dag_obs_set_t* obs) {
  spn_target_unit_t* unit = (spn_target_unit_t*)user_data;
  spn_path_t object = outputs[0];
  spn_path_t header = outputs[1];
  spn_target_info_t* info = unit->info;
  sp_str_t obj = spn_path_str(&spn.roots, spn.mem, object);
  sp_str_t hdr = spn_path_str(&spn.roots, spn.mem, header);

  spn_pkg_unit_announce_compile(unit->pkg);

  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_EMBED_START,
    .pkg = unit->pkg->info->name,
    .embed_start = { .target = info->name, .num_files = sp_da_size(info->embed) },
  });

  sp_tm_timer_t timer = sp_tm_start_timer();

  spn_cc_embed_ctx_t embedder = sp_zero;
  spn_cc_embed_ctx_init(&embedder, spn.mem, spn_os_format(unit->pkg->build->profile.os), unit->pkg->build->profile.arch);

  sp_da_for(info->embed, it) {
    spn_embed_t embed = info->embed[it];
    spn_embed_types_t types = embed.types;

    if (sp_str_empty(types.data)) {
      types.data = sp_str_lit("unsigned char");
    }

    if (sp_str_empty(types.size)) {
      types.size = sp_str_lit("unsigned long long");
    }

    switch (embed.kind) {
      case SPN_EMBED_FILE: {
        spn_dag_observe(obs, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_FILE, .path = embed.path });
        sp_str_buf_t buf = sp_zero;
        sp_str_t file = spn_path_str(&spn.roots, sp_str_buf_as_mem(&buf), embed.path);
        sp_str_t content = sp_zero;
        if (sp_io_read_file(embedder.mem, file, &content) != SP_OK) {
          spn_event_buffer_push(spn.events, (spn_event_t) {
            .kind = SPN_EVENT_EMBED_FAILED,
            .pkg = unit->pkg->info->name,
            .embed_failed = { .target = info->name, .path = sp_str_copy(spn.mem, file), .error = sp_str_lit("file not found") },
          });
          return SPN_ERR_DAG_ACTION;
        }

        sp_mem_buffer_t data = {
          .data = (u8*)(uintptr_t)content.data,
          .len = content.len,
          .capacity = content.len,
        };
        sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
        spn_cc_embed_ctx_add(&embedder, data, spn_cc_symbol_from_embedded_file(scratch.mem, embed.dest), embed.dest, types.data, types.size);
        sp_mem_end_scratch(scratch);
        break;
      }
      case SPN_EMBED_DIR: {
        sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
        spn_path_t root = embed.path;
        sp_str_buf_t buf = sp_zero;
        sp_str_t dir = spn_path_str(&spn.roots, sp_str_buf_as_mem(&buf), root);
        spn_dag_observe(obs, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_ENUMERATION, .path = root });
        u32 start = dir.len + 1;
        sp_fs_it_t walk = sp_fs_it_new_recursive(scratch.mem, dir);
        while (sp_fs_it_next(&walk)) {
          sp_fs_entry_t entry = walk.entry;
          sp_str_t rel = sp_str_suffix(entry.path, (s32)(entry.path.len - start));
          if (entry.kind == SP_FS_KIND_DIR) {
            spn_dag_observe(obs, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_ENUMERATION, .path = spn_path_join(scratch.mem, root, rel) });
            continue;
          }
          if (!sp_fs_is_file(entry.path)) continue;
          spn_dag_observe(obs, (spn_dag_obs_t) { .kind = SPN_DAG_OBS_FILE, .path = spn_path_join(scratch.mem, root, rel) });
          sp_str_t content = sp_zero;
          if (sp_io_read_file(embedder.mem, entry.path, &content) != SP_OK) {
            spn_event_buffer_push(spn.events, (spn_event_t) {
              .kind = SPN_EVENT_EMBED_FAILED,
              .pkg = unit->pkg->info->name,
              .embed_failed = { .target = info->name, .path = sp_str_copy(spn.mem, entry.path), .error = sp_str_lit("file not found") },
            });
            sp_fs_it_deinit(&walk);
            sp_mem_end_scratch(scratch);
            return SPN_ERR_DAG_ACTION;
          }
          sp_mem_buffer_t entry_data = {
            .data = (u8*)(uintptr_t)content.data,
            .len = content.len,
            .capacity = content.len,
          };
          sp_str_t dest = sp_fs_join_path(scratch.mem, embed.dest, rel);
          spn_cc_embed_ctx_add(&embedder, entry_data, spn_cc_symbol_from_embedded_file(scratch.mem, dest), dest, types.data, types.size);
        }
        sp_fs_it_deinit(&walk);
        sp_mem_end_scratch(scratch);
        break;
      }
    }
  }

  spn_err_t write_err = spn_cc_embed_ctx_write(&embedder, obj, hdr);
  spn_cc_embed_ctx_free(&embedder);
  if (write_err) {
    spn_event_buffer_push(spn.events, (spn_event_t) {
      .kind = SPN_EVENT_EMBED_FAILED,
      .pkg = unit->pkg->info->name,
      .embed_failed = { .target = info->name, .error = sp_str_lit("embed write failed") },
    });
    return SPN_ERR_DAG_ACTION;
  }

  u64 elapsed = sp_tm_read_timer(&timer);
  spn_event_buffer_push(spn.events, (spn_event_t) {
    .kind = SPN_EVENT_EMBED_PASSED,
    .pkg = unit->pkg->info->name,
    .embed_passed = { .target = info->name, .object_path = obj, .header_path = hdr, .time = elapsed },
  });

  return SPN_OK;
}
