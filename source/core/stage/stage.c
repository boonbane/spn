#include "stage/stage.h"
#include "dag/dag.h"
#include "paths/paths.h"
#include "str/str.h"

typedef struct {
  sp_str_t key;
  sp_str_t to;
} row_t;

static sp_da(row_t) read_rows(sp_mem_t mem, sp_str_t manifest) {
  sp_da(row_t) rows = sp_da_new(mem, row_t);
  sp_da(sp_str_t) lines = sp_str_split_c8(mem, manifest, '\n');
  sp_da_for(lines, it) {
    sp_str_pair_t pair = sp_str_cleave_c8(lines[it], '\t');
    if (!sp_str_empty(pair.second)) {
      sp_da_push(rows, ((row_t) { .key = pair.first, .to = pair.second }));
    }
  }
  return rows;
}

static sp_err_t remove_path(const spn_path_roots_t* roots, spn_path_t path) {
  sp_err_t err = sp_fs_remove_file_at(spn_path_at(roots, path));
  if (err && err != SP_ERR_SYS_NOT_FOUND) {
    return err;
  }
  for (spn_path_t dir = spn_path_parent(path); !sp_str_empty(dir.sub); dir = spn_path_parent(dir)) {
    sp_path_t at = spn_path_at(roots, dir);
    if (sp_sys_rmdir_s(at.dir, at.sub)) {
      break;
    }
  }
  return SP_OK;
}

spn_stage_diag_t spn_stage_apply(sp_mem_t scratch, const spn_path_roots_t* roots, spn_dag_file_cache_t* files, spn_path_t manifest, si_da(sp_str_t) declared, si_da(spn_stage_t) rows) {
  sp_str_t previous = sp_zero;
  sp_err_t read = sp_io_read_file_at(scratch, spn_path_at(roots, manifest), &previous);
  if (read && read != SP_ERR_SYS_NOT_FOUND) {
    return (spn_stage_diag_t) { .err = SPN_ERR_FS_READ, .path = manifest };
  }
  sp_da(row_t) old = read_rows(scratch, previous);

  sp_str_ht(bool) is_declared = SP_NULLPTR;
  sp_str_ht(bool) is_applied = SP_NULLPTR;
  sp_str_ht(bool) was_staged = SP_NULLPTR;
  sp_str_ht(bool) staged = SP_NULLPTR;
  sp_str_ht_init(scratch, is_declared);
  sp_str_ht_init(scratch, is_applied);
  sp_str_ht_init(scratch, was_staged);
  sp_str_ht_init(scratch, staged);
  si_da_for(declared, it) {
    sp_str_ht_insert(is_declared, declared[it], true);
  }

  sp_da(row_t) next = sp_da_new(scratch, row_t);
  si_da_for(rows, it) {
    sp_assert(rows[it].to.root == manifest.root);
    sp_str_ht_insert(is_applied, rows[it].key, true);
    sp_str_ht_insert(staged, rows[it].to.sub, true);
    sp_da_push(next, ((row_t) { .key = rows[it].key, .to = rows[it].to.sub }));
  }
  sp_da_for(old, it) {
    sp_str_ht_insert(was_staged, old[it].to, true);
    if (sp_str_ht_get(is_declared, old[it].key) && !sp_str_ht_get(is_applied, old[it].key)) {
      sp_str_ht_insert(staged, old[it].to, true);
      sp_da_push(next, old[it]);
    }
  }

  si_da_for(rows, it) {
    const spn_stage_t* row = &rows[it];
    sp_sys_file_meta_t meta = sp_zero;
    spn_dag_digest_t digest = sp_zero;
    if (sp_str_ht_get(was_staged, row->to.sub) || spn_dag_file_cache_stat(files, row->to, &meta)) {
      continue;
    }
    if (meta.kind != SP_FS_KIND_FILE || spn_dag_file_cache_digest(files, row->to, &digest) || !spn_dag_digest_equal(digest, row->digest)) {
      return (spn_stage_diag_t) { .err = SPN_ERR_STAGE_COLLISION, .path = row->to };
    }
  }

  sp_da_for(old, it) {
    if (sp_str_ht_get(staged, old[it].to)) {
      continue;
    }
    spn_path_t stale = { .root = manifest.root, .sub = old[it].to };
    sp_err_t removed = remove_path(roots, stale);
    spn_dag_file_cache_invalidate(files, stale);
    if (removed) {
      return (spn_stage_diag_t) { .err = SPN_ERR_FS_REMOVE, .path = stale };
    }
  }

  si_da_for(rows, it) {
    const spn_stage_t* row = &rows[it];
    sp_sys_file_meta_t meta = sp_zero;
    spn_dag_digest_t digest = sp_zero;
    if (!spn_dag_file_cache_stat(files, row->to, &meta) && meta.nlink == 1 &&
        !spn_dag_file_cache_digest(files, row->to, &digest) &&
        spn_dag_digest_equal(digest, row->digest)) {
      continue;
    }
    sp_fs_create_parent_at(spn_path_at(roots, row->to));
    sp_err_t copied = sp_fs_copy_file_at(spn_path_at(roots, row->location), spn_path_at(roots, row->to), SP_FS_ATOMIC_REPLACE);
    if (copied || spn_dag_file_cache_seed(files, row->to, row->digest)) {
      spn_dag_file_cache_invalidate(files, row->to);
      return (spn_stage_diag_t) { .err = SPN_ERR_FS_WRITE, .path = row->to };
    }
  }

  sp_io_dyn_mem_writer_t io = sp_zero;
  sp_io_dyn_mem_writer_init(scratch, &io);
  sp_da_for(next, it) {
    sp_fmt_io(&io.base, "{}\t{}\n", sp_fmt_str(next[it].key), sp_fmt_str(next[it].to));
  }
  sp_str_t content = sp_io_dyn_mem_writer_as_str(&io);
  if (!sp_str_equal(previous, content)) {
    sp_fs_create_parent_at(spn_path_at(roots, manifest));
    if (sp_fs_write_atomic_at(spn_path_at(roots, manifest), content)) {
      return (spn_stage_diag_t) { .err = SPN_ERR_FS_WRITE, .path = manifest };
    }
  }
  return (spn_stage_diag_t) sp_zero;
}

spn_stage_diag_t spn_stage_clean(sp_mem_t scratch, const spn_path_roots_t* roots, spn_path_t manifest) {
  sp_str_t content = sp_zero;
  sp_err_t read = sp_io_read_file_at(scratch, spn_path_at(roots, manifest), &content);
  if (read == SP_ERR_SYS_NOT_FOUND) {
    return (spn_stage_diag_t) sp_zero;
  }
  if (read) {
    return (spn_stage_diag_t) { .err = SPN_ERR_FS_READ, .path = manifest };
  }
  sp_da(row_t) rows = read_rows(scratch, content);
  sp_da_for(rows, it) {
    spn_path_t staged = { .root = manifest.root, .sub = rows[it].to };
    if (remove_path(roots, staged)) {
      return (spn_stage_diag_t) { .err = SPN_ERR_FS_REMOVE, .path = staged };
    }
  }
  if (remove_path(roots, manifest)) {
    return (spn_stage_diag_t) { .err = SPN_ERR_FS_REMOVE, .path = manifest };
  }
  return (spn_stage_diag_t) sp_zero;
}
