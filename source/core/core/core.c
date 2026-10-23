#include "core/core.h"
#include "fs/fs.h"
#include "io/io.h"

static bool content_matches(sp_mem_t mem, sp_path_t path, sp_mem_slice_t content) {
  sp_mem_slice_t existing = sp_zero;
  if (sp_io_read_file_slice(mem, path, &existing)) {
    return false;
  }
  return existing.len == content.len && sp_mem_is_equal(existing.data, content.data, content.len);
}

spn_err_t spn_fs_update_file(sp_path_t from, sp_path_t to) {
  sp_sys_file_meta_t source = sp_zero;
  if (sp_sys_get_path_metadata_s(from.dir, from.sub, &source)) {
    return SPN_ERROR;
  }

  sp_mem_arena_marker_t s = sp_mem_begin_scratch();

  sp_sys_file_meta_t dest = sp_zero;
  sp_sys_get_path_metadata_s(to.dir, to.sub, &dest);

  bool matches = false;
  if (dest.kind == SP_FS_KIND_FILE && dest.size == source.size) {
    sp_mem_slice_t content = sp_zero;
    matches = !sp_io_read_file_slice(s.mem, from, &content) && content_matches(s.mem, to, content);
  }

  spn_err_t err = SPN_OK;
  if (!matches) {
    sp_fs_create_parent_at(to);
    if (sp_fs_copy_file_at(from, to, SP_FS_ATOMIC_REPLACE)) {
      err = SPN_ERROR;
    }
  }

  sp_mem_end_scratch(s);
  return err;
}

void spn_wake_ring(spn_wake_t* wake) {
  if (!wake->fn) {
    return;
  }
  if (sp_atomic_u32_cas(&wake->signaled, 0, 1, SP_ATOMIC_SEQ_CST)) {
    wake->fn(wake->data);
  }
}

void spn_wake_pulse(spn_wake_t* wake) {
  if (!wake->fn) {
    return;
  }
  sp_atomic_u32_store(&wake->signaled, 1, SP_ATOMIC_SEQ_CST);
  wake->fn(wake->data);
}

void spn_wake_rearm(spn_wake_t* wake) {
  sp_atomic_u32_store(&wake->signaled, 0, SP_ATOMIC_SEQ_CST);
}
