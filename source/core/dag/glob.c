#include "dag/glob.h"
#include "dag/types.h"
#include "glob/glob.h"
#include "paths/paths.h"
#include "sp.h"
#include "spn/core.h"
#include "sp/sp_glob.h"
#include "str/str.h"

static s32 compare_matches(const void* a, const void* b) {
  const spn_dag_glob_match_t* pa = sp_ptr_cast(const spn_dag_glob_match_t*, a);
  const spn_dag_glob_match_t* pb = sp_ptr_cast(const spn_dag_glob_match_t*, b);
  return sp_str_compare_alphabetical(pa->path.sub, pb->path.sub);
}

spn_dag_glob_it_t spn_dag_glob_it_new(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t pattern) {
  sp_glob_meta_t meta = sp_glob_parse_meta(pattern.sub);

  spn_dag_glob_it_t it = {
    .glob = sp_glob_new_str(mem, meta.rest),
    .base = { pattern.root, meta.dir },
    .recursive = meta.deep,
    .dir = sp_sys_dir(SP_SYS_INVALID_FD),
  };
  if (!it.glob) {
    it.err = SPN_ERR_DAG_GLOB;
    return it;
  }

  sp_err_t err = sp_fs_open_dir_at(spn_path_at(roots, it.base), &it.dir);
  if (err == SP_ERR_SYS_NOT_FOUND) {
    return it;
  }
  if (err) {
    it.err = SPN_ERR_DAG_GLOB;
    return it;
  }
  it.fs = sp_fs_it(mem, it.dir);
  return it;
}

bool spn_dag_glob_it_next(spn_dag_glob_it_t* it) {
  if (it->err || it->dir.fd == SP_SYS_INVALID_FD) {
    return false;
  }
  spn_path_t build = spn_path_project_build();
  bool root = it->base.root == build.root && sp_str_empty(it->base.sub);
  sp_fs_it_entry_t entry = sp_zero;
  while (sp_fs_it_next(&it->fs, &entry)) {
    if (entry.kind == SP_FS_KIND_DIR) {
      if (!it->recursive || (root && sp_str_equal(entry.path, build.sub))) {
        sp_fs_it_skip(&it->fs);
        continue;
      }
    }
    else if (!sp_glob_match(it->glob, entry.path)) {
      continue;
    }
    it->entry = (spn_dag_glob_entry_t) { .rel = entry.path, .kind = entry.kind };
    return true;
  }
  if (it->fs.err) {
    it->err = SPN_ERR_DAG_GLOB;
  }
  return false;
}

void spn_dag_glob_it_deinit(spn_dag_glob_it_t* it) {
  if (it->dir.fd != SP_SYS_INVALID_FD) {
    sp_fs_it_deinit(&it->fs);
    sp_fs_close_dir(it->dir);
  }
  sp_glob_free(it->glob);
}


spn_err_t spn_dag_glob(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t pattern, spn_dag_glob_result_t* result) {
  result->obs = sp_da_new(mem, spn_dag_obs_t);
  result->matches = sp_da_new(mem, spn_dag_glob_match_t);

  sp_glob_meta_t glob = sp_glob_parse_meta(pattern.sub);
  spn_dag_glob_it_t it = spn_dag_glob_it_new(mem, roots, pattern);
  sp_da_push(result->obs, ((spn_dag_obs_t) {
    .kind = SPN_DAG_OBS_ENUMERATION,
    .path = spn_path_copy(mem, it.base),
    .filter = glob.name,
  }));
  while (spn_dag_glob_it_next(&it)) {
    spn_path_t path = spn_path_join(mem, it.base, it.entry.rel);
    if (it.entry.kind == SP_FS_KIND_DIR) {
      sp_da_push(result->obs, ((spn_dag_obs_t) { .kind = SPN_DAG_OBS_ENUMERATION, .path = path, .filter = glob.name }));
      continue;
    }
    sp_da_push(result->obs, ((spn_dag_obs_t) {
      .kind = SPN_DAG_OBS_FILE,
      .path = path
    }));
    sp_da_push(result->matches, ((spn_dag_glob_match_t) {
      .path = path,
      .rel = sp_str_suffix(path.sub, (s32)it.entry.rel.len),
    }));
  }
  spn_dag_glob_it_deinit(&it);
  spn_try(it.err);

  sp_da_sort(result->matches, compare_matches);
  return SPN_OK;
}
