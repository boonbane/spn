#ifndef SPN_CODEGEN_LOWER_H
#define SPN_CODEGEN_LOWER_H

#include "codegen/codegen.h"
#include "codegen/toolchain.h"
#include "sp.h"
#include "spn/core.h"
#include "index/types.h"
#include "spn/types.h"
#include "common.gen.h"
#include "manifest.gen.h"
#include "pkg/types.h"

spn_err_t spn_manifest_lower(spn_toml_loader_t* ctx, const spn_cg_manifest_t* cg, si_pkg_info_t* manifest);

spn_err_t spn_manifest_lower_patch_hashes(spn_toml_loader_t* ctx, si_pkg_info_t* manifest);

spn_err_t spn_manifest_reject_patches(spn_toml_loader_t* ctx, si_pkg_info_t* manifest);
spn_index_info_t spn_index_lower(spn_toml_loader_t* ctx, u32 at, spn_index_kind_t kind, const spn_cg_index_t* decl);

spn_err_t spn_codegen_load_manifest(spn_toml_loader_t* ctx, spn_path_t path, si_pkg_info_t* manifest);

#endif
