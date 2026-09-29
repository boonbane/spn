#include "project/project.h"

#include "error/error.h"
#include "intern/intern.h"
#include "lock/lock.h"
#include "paths/paths.h"
#include "pkg/load.h"
#include "toml/issue.h"

spn_err_t spn_project_load(spn_ctx_t* ctx, spn_project_t** project) {
  *project = SP_NULLPTR;

  spn_path_t manifest = { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_str_lit("spn.toml") };
  if (!sp_fs_exists_at(spn_path_at(&ctx->roots, manifest))) {
    return SPN_OK;
  }

  spn_project_t* loaded = sp_alloc_type(ctx->heap, spn_project_t);
  loaded->paths.manifest = manifest;
  loaded->paths.lock = (spn_path_t) { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_str_lit("spn.lock") };

  spn_codegen_issues_t issues = sp_zero;
  spn_err_t parsed = spn_pkg_load(ctx->heap, ctx->intern, spn_path_at(&ctx->roots, manifest), SPN_MANIFEST_ROOT, &loaded->package, &issues);
  if (parsed == SPN_ERR_NO_MANIFEST) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_NO_MANIFEST,
      .no_manifest = { .path = spn_path_str(&ctx->roots, ctx->heap, manifest) },
    });
  }
  if (parsed) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_MANIFEST_ISSUES,
      .manifest = { .path = spn_path_str(&ctx->roots, ctx->heap, manifest), .issues = spn_codegen_issues_to_err(ctx->heap, issues) },
    });
  }

  sp_path_t lock = spn_path_at(&ctx->roots, loaded->paths.lock);
  if (sp_fs_exists_at(lock)) {
    sp_opt_set(loaded->lock, spn_lock_file_load(ctx->heap, lock, ctx->events));
  }

  *project = loaded;
  return SPN_OK;
}

spn_err_t spn_project_update_lock(spn_ctx_t* ctx, spn_project_t* project, spn_resolve_t resolve) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_lock_file_t lock = spn_build_lock_file(scratch.mem, ctx->intern, resolve, &project->package);

  sp_da_for(project->package.system_deps, it) {
    sp_ht_insert(lock.system_deps, project->package.system_deps[it], true);
  }

  sp_str_t output = spn_lock_file_to_str(scratch.mem, &lock);
  sp_err_t written = sp_fs_write_atomic_at(spn_path_at(&ctx->roots, project->paths.lock), output);
  sp_mem_end_scratch(scratch);

  if (written != SP_OK) {
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = project->paths.lock } });
  }
  return SPN_OK;
}
