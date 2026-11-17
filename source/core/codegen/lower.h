#ifndef SPN_CODEGEN_LOWER_H
#define SPN_CODEGEN_LOWER_H

#include "codegen/codegen.h"
#include "codegen/toolchain.h"
#include "sp.h"
#include "spn/core.h"
#include "dag/types.h"
#include "external/net.h"
#include "index/types.h"
#include "spn/types.h"
#include "common.gen.h"
#include "config.gen.h"
#include "manifest.gen.h"
#include "pkg/types.h"
#include "project/types.h"

void             spn_manifest_lower(spn_toml_loader_t* ctx, const spn_cg_manifest_t* cg, si_pkg_info_t* pkg);
void             spn_project_lower(spn_toml_loader_t* ctx, const spn_cg_manifest_t* cg, spn_project_t* project);
spn_index_info_t spn_index_lower(spn_toml_loader_t* ctx, u32 at, spn_index_kind_t kind, const spn_cg_index_t* decl);
spn_net_config_t spn_net_lower(spn_toml_loader_t* ctx, const spn_cg_net_t* cg);
spn_cache_config_t spn_cache_lower(const spn_cg_cache_t* cg);

#endif
