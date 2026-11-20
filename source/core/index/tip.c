#include "index/tip.h"

#include "git/remote.h"

#define tip_read "spn-tip"
#define tip_failed "spn-failed"

static bool recent(sp_path_t stamp, u32 window) {
  sp_sys_file_meta_t meta = sp_zero;
  u64 modified = sp_fs_get_path_metadata_at(stamp, &meta) ? 0 : (u64)meta.mtime.tv_sec;
  return modified + window > sp_tm_now_epoch().s;
}

si_index_tip_t si_index_tip_plan(sp_mem_t mem, const spn_index_info_t* index, bool force) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  sp_path_t read = sp_path_join(scratch.mem, index->location.at, sp_str_lit(tip_read));
  sp_path_t failed = sp_path_join(scratch.mem, index->location.at, sp_str_lit(tip_failed));

  sp_str_t rev = sp_zero;
  sp_fs_read_at(mem, read, &rev);
  rev = sp_str_trim(rev);

  bool fresh = !sp_str_empty(rev) && recent(read, index->refresh);
  bool fetch = force || sp_str_empty(rev) || (!fresh && !recent(failed, index->refresh));
  sp_mem_end_scratch(scratch);

  return (si_index_tip_t) { .rev = rev, .fetch = fetch, .held = !fetch && !fresh };
}

spn_err_t si_index_tip_fetch(sp_mem_t mem, const spn_index_info_t* index, sp_atomic_s32_t* cancelled, sp_str_t* rev, spn_git_failure_t* failure) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch_for(mem);
  sp_path_t read = sp_path_join(scratch.mem, index->location.at, sp_str_lit(tip_read));
  sp_path_t failed = sp_path_join(scratch.mem, index->location.at, sp_str_lit(tip_failed));

  spn_err_t err = si_git_tip(index->remote, mem, index->git.url, index->backoff, cancelled, rev, failure);
  if (!err) {
    sp_fs_create_parent_at(read);
    sp_fs_write_at(read, *rev, SP_FS_WRITE_REPLACE, sp_sys_default_file_perms);
    sp_fs_remove_file_at(failed);
  }
  else if (failure->kind != SPN_GIT_CANCELLED) {
    sp_fs_create_parent_at(failed);
    sp_fs_write_at(failed, sp_str_lit(""), SP_FS_WRITE_TRUNCATE, sp_sys_default_file_perms);
  }

  sp_mem_end_scratch(scratch);
  return err;
}
