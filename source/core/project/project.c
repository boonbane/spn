#include "project/project.h"

#include "codegen/codegen.h"
#include "codegen/lower.h"
#include "error/error.h"
#include "git/patch.h"
#include "lock/lock.h"
#include "paths/paths.h"
#include "toml/issue.h"
#include "toml/loader.h"

spn_err_t spn_project_load(spn_ctx_t* ctx, spn_project_t** project) {
  *project = SP_NULLPTR;

  spn_path_t manifest = { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_str_lit("spn.toml") };
  if (!sp_fs_exists_at(spn_path_at(&ctx->roots, manifest))) {
    return SPN_OK;
  }

  spn_project_t* loaded = sp_alloc_type(ctx->heap, spn_project_t);
  loaded->paths.manifest = manifest;
  loaded->paths.lock = (spn_path_t) { .root = SPN_PATH_ROOT_PROJECT, .sub = sp_str_lit("spn.lock") };

  spn_toml_loader_t t = sp_zero;
  spn_toml_loader_init(&t, ctx->heap, ctx->intern, &ctx->roots);
  t.strict = true;

  spn_cg_manifest_t cg = sp_zero;
  if (!spn_codegen_load(&t, manifest, &cg)) {
    spn_manifest_lower(&t, &cg, &loaded->package);
    spn_project_lower(&t, &cg, loaded);
  }
  if (!sp_da_empty(t.issues)) {
    return spn_err_emit(ctx, (spn_err_union_t) {
      .kind = SPN_ERR_MANIFEST_ISSUES,
      .manifest = { .path = spn_path_str(&ctx->roots, ctx->events->mem, manifest), .issues = spn_codegen_issues_to_err(ctx->events->mem, t.issues) },
    });
  }

  spn_err_t hashed = SPN_OK;
  si_da_for_ptr(loaded->patches, patch) {
    u32 missing = 0;
    if (spn_git_patch_set_hash(&patch->set, &missing)) {
      hashed = spn_err_emit(ctx, (spn_err_union_t) {
        .kind = SPN_ERR_PATCH_FILE_MISSING,
        .patch = { .name = patch->qualified.str, .path = patch->set.files[missing] },
      });
    }
  }
  spn_try(hashed);

  sp_path_t lock = spn_path_at(&ctx->roots, loaded->paths.lock);
  if (sp_fs_exists_at(lock)) {
    sp_opt_set(loaded->lock, spn_lock_file_load(ctx->heap, lock, ctx->events));
  }

  *project = loaded;
  return SPN_OK;
}

spn_err_t spn_project_update_lock(spn_ctx_t* ctx, spn_project_t* project, spn_resolve_t resolve) {
  sp_mem_arena_marker_t scratch = sp_mem_begin_scratch();
  spn_lock_file_t lock = spn_build_lock_file(scratch.mem, ctx->intern, resolve);

  sp_str_t output = spn_lock_file_to_str(scratch.mem, &lock);
  sp_err_t written = sp_fs_write_atomic_at(spn_path_at(&ctx->roots, project->paths.lock), output);
  sp_mem_end_scratch(scratch);

  if (written != SP_OK) {
    return spn_err_emit(ctx, (spn_err_union_t) { .kind = SPN_ERR_FS_WRITE, .fs = { .path = project->paths.lock } });
  }
  return SPN_OK;
}
