#include "sp.h"
#include "pkg/patch.h"

spn_pkg_patch_stamp_result_t spn_pkg_patch_stamp(si_da(spn_pkg_patch_t) patches, sp_intern_id_t qualified, spn_pkg_root_t* source) {
  si_da_for(patches, it) {
    if (patches[it].qualified.id != qualified) {
      continue;
    }
    if (source->kind != SPN_PKG_ROOT_GIT) {
      return SPN_PKG_PATCH_STAMP_NOT_GIT;
    }
    source->git.patches = patches[it].set;
    return SPN_PKG_PATCH_STAMP_APPLIED;
  }
  return SPN_PKG_PATCH_STAMP_NONE;
}
