#include "dag/dag.h"
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
  };
  if (!it.glob) {
    it.err = SPN_ERR_DAG_GLOB;
  }

  sp_str_buf_t str = sp_zero;
  sp_str_t path = spn_path_str(roots, sp_str_buf_as_mem(&str), it.base);
  it.start = path.len + 1;
  it.fs = meta.deep ? sp_fs_it_new_recursive(mem, path) : sp_fs_it_new(mem, path);
  if (it.fs.err == SP_ERR_SYS_NOT_FOUND) {
    it.fs.err = SP_OK;
  }
  return it;
}

bool spn_dag_glob_it_next(spn_dag_glob_it_t* it) {
  if (it->err) {
    return false;
  }
  while (sp_fs_it_next(&it->fs)) {
    sp_fs_entry_t entry = it->fs.entry;
    sp_str_t rel = sp_str_suffix(entry.path, (s32)(entry.path.len - it->start));
    if (entry.kind == SP_FS_KIND_DIR && !it->recursive) {
      continue;
    }
    if (entry.kind != SP_FS_KIND_DIR && !sp_glob_match(it->glob, rel)) {
      continue;
    }
    it->entry = (spn_dag_glob_entry_t) { .rel = rel, .kind = entry.kind };
    return true;
  }
  if (it->fs.err) {
    it->err = SPN_ERR_DAG_GLOB;
  }
  return false;
}

void spn_dag_glob_it_deinit(spn_dag_glob_it_t* it) {
  sp_fs_it_deinit(&it->fs);
  sp_glob_free(it->glob);
}


spn_err_t spn_dag_glob(sp_mem_t mem, const spn_path_roots_t* roots, spn_path_t pattern, spn_dag_glob_result_t* result) {
  result->obs = sp_da_new(mem, spn_dag_obs_t);
  result->matches = sp_da_new(mem, spn_dag_glob_match_t);

  sp_glob_meta_t glob = sp_glob_parse_meta(pattern.sub);
  if (glob.literal) {
    spn_dag_obs_t observation = sp_zero;

    sp_sys_file_meta_t file = sp_zero;
    switch (spn_get_path_metadata(roots, pattern, &file)) {
      case SP_OK: observation.kind = SPN_DAG_OBS_FILE; break;
      case SP_ERR_SYS_NOT_FOUND: observation.kind = SPN_DAG_OBS_ABSENT; break;
      default: return SPN_ERR_DAG_GLOB;
    }

    observation.path = spn_path_copy(mem, pattern);
    sp_da_push(result->obs, observation);

    if (file.kind == SP_FS_KIND_FILE) {
      sp_da_push(result->matches, ((spn_dag_glob_match_t) {
        .path = observation.path,
        .rel = sp_str_suffix(observation.path.sub, (s32)glob.name.len)
      }));
    }
    return SPN_OK;
  }

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
