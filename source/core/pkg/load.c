#include "pkg/load.h"

#include "codegen/codegen.h"
#include "codegen/lower.h"
#include "paths/paths.h"
#include "toml/issue.h"
#include "toml/loader.h"

spn_err_t spn_pkg_load(sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, sp_str_t name, spn_path_t path, si_pkg_info_t* pkg, spn_err_union_t* err) {
  if (!sp_fs_exists_at(spn_path_at(roots, path))) {
    *err = (spn_err_union_t) {
      .kind = SPN_ERR_NO_MANIFEST,
      .no_manifest = { .path = spn_path_str(roots, mem, path) },
    };
    return err->kind;
  }

  spn_toml_loader_t t = sp_zero;
  spn_toml_loader_init(&t, mem, intern, roots);
  t.strict = true;

  spn_cg_manifest_t cg = sp_zero;
  if (!spn_codegen_load(&t, path, &cg)) {
    spn_manifest_lower(&t, &cg, pkg);
    if (!sp_da_empty(cg.patch)) {
      spn_toml_loader_issue(&t, SPN_ERR_CODEGEN_ROOT_ONLY, "patch");
    }
  }

  if (sp_da_empty(t.issues)) {
    return SPN_OK;
  }

  *err = (spn_err_union_t) {
    .kind = SPN_ERR_MANIFEST_ISSUES,
    .manifest = {
      .name = name,
      .path = spn_path_str(roots, mem, path),
      .issues = spn_codegen_issues_to_err(mem, t.issues),
    },
  };
  return err->kind;
}
