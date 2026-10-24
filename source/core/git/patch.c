#include "sp.h"
#include "git/patch.h"
#include "hash/digest/digest.h"

sp_hash_t spn_git_patch_hash(si_da(sp_str_t) files, u32* missing) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();

  sp_hash_t hash = 0;
  si_da_for(files, it) {
    sp_str_t content = sp_zero;
    if (sp_io_read_file_at(scratch.mem, sp_path_from_str(files[it]), &content)) {
      *missing = (u32)it;
      sp_mem_end_scratch(scratch);
      return 0;
    }
    sp_hash_t parts [] = {
      hash,
      spn_digest_hash_str(content),
    };
    hash = spn_digest_hash_combine(parts, sp_carr_len(parts));
  }

  sp_mem_end_scratch(scratch);
  sp_assert(si_da_empty(files) || hash);
  return hash;
}
