#include "pkg/load.h"

#include "codegen/lower.h"
#include "manifest.gen.h"
#include "toml/loader.h"

spn_err_t spn_pkg_load(sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots, spn_path_t path, si_pkg_info_t* pkg, spn_err_union_t* err) {
  spn_toml_loader_t t = spn_toml_loader(mem, intern, roots);
  spn_cg_manifest_t cg = sp_zero;
  spn_toml_load_manifest(&t, path, &cg);
  if (sp_da_empty(t.issues)) {
    spn_manifest_lower(&t, &cg, pkg);
    if (!sp_da_empty(cg.patch)) {
      spn_toml_loader_issue(&t, SPN_ERR_CODEGEN_ROOT_ONLY, "patch");
    }
  }
  if (sp_da_empty(t.issues)) {
    return SPN_OK;
  }
  *err = spn_toml_loader_err(&t, path);
  return err->kind;
}
