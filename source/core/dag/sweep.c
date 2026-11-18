#include "dag/dag.h"
#include "dag/types.h"
#include "paths/paths.h"
#include "sp.h"
#include "spn/core.h"

#define sweep_block_size 4096

typedef struct {
  sp_path_t path;
  sp_fs_kind_t kind;
  sp_sys_timespec_t used;
  u64 bytes;
} item_t;

static s32 item_order(const void* a, const void* b) {
  sp_sys_timespec_t ta = ((const item_t*)a)->used;
  sp_sys_timespec_t tb = ((const item_t*)b)->used;
  if (ta.tv_sec != tb.tv_sec) {
    return ta.tv_sec < tb.tv_sec ? -1 : 1;
  }
  if (ta.tv_nsec != tb.tv_nsec) {
    return ta.tv_nsec < tb.tv_nsec ? -1 : 1;
  }
  return 0;
}

static void collect(sp_mem_t mem, sp_path_t dir, si_da(item_t)* items) {
  sp_sys_dir_t root = sp_zero;
  if (sp_fs_open_dir_at(dir, &root)) {
    return;
  }

  sp_fs_it_t walk = sp_fs_it(mem, root);
  sp_fs_it_entry_t entry = sp_zero;
  while (sp_fs_it_next(&walk, &entry)) {
    sp_sys_file_meta_t meta = sp_zero;
    if (sp_fs_get_link_metadata_at(entry.at, &meta)) {
      sp_fs_it_skip(&walk);
      continue;
    }
    if (sp_str_find_c8(entry.path, '/') == SP_STR_NO_MATCH) {
      si_da_push(mem, *items, ((item_t) {
        .path = sp_path_join(mem, dir, entry.path),
        .kind = meta.kind,
        .used = meta.mtime,
      }));
    }
    if (meta.kind == SP_FS_KIND_FILE) {
      si_da_back(*items)->bytes += sp_align_offset((u64)meta.size, sweep_block_size);
    }
  }
  sp_fs_it_deinit(&walk);
  sp_fs_close_dir(root);
}

spn_dag_sweep_t spn_dag_sweep(sp_mem_t mem, const spn_path_roots_t* roots, const spn_dag_layout_t* layout, spn_dag_sweep_limits_t limits) {
  si_da(item_t) items = SP_NULLPTR;
  collect(mem, spn_path_at(roots, layout->strong), &items);
  collect(mem, spn_path_at(roots, layout->weak), &items);
  collect(mem, spn_path_at(roots, layout->store), &items);

  spn_dag_sweep_t sweep = sp_zero;
  si_da_for(items, it) {
    sweep.bytes += items[it].bytes;
  }
  if (sweep.bytes <= limits.max_bytes) {
    return sweep;
  }

  si_da_sort(items, item_order);
  u64 now = sp_tm_now_epoch().s;
  si_da_for(items, it) {
    item_t* item = &items[it];
    u64 used = (u64)item->used.tv_sec;
    if (sweep.bytes - sweep.freed <= limits.keep_bytes || used > now || now - used < limits.min_age_s) {
      break;
    }

    sp_sys_file_meta_t meta = sp_zero;
    if (sp_fs_get_link_metadata_at(item->path, &meta) || meta.mtime.tv_sec != item->used.tv_sec || meta.mtime.tv_nsec != item->used.tv_nsec) {
      continue;
    }
    if (item->kind == SP_FS_KIND_DIR ? sp_fs_remove_dir_at(item->path) : sp_fs_remove_file_at(item->path)) {
      continue;
    }
    sweep.freed += item->bytes;
    sweep.removed++;
  }
  return sweep;
}
