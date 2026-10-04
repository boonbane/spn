#include "path_roots.h"

const spn_path_roots_t* paths_test_roots_build(paths_test_roots_t spec, spn_path_roots_t* storage) {
  const c8* dirs [SPN_PATH_ROOT_COUNT] = {
    [SPN_PATH_ROOT_PROJECT] = spec.project,
    [SPN_PATH_ROOT_STORAGE] = spec.storage,
    [SPN_PATH_ROOT_TOOLCHAIN] = spec.toolchain,
  };

  *storage = (spn_path_roots_t) sp_zero;
  sp_carr_for(dirs, it) {
    if (dirs[it]) {
      storage->dirs[it] = sp_str_view(dirs[it]);
    }
  }
  return storage;
}
