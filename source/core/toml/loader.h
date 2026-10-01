#ifndef SPN_TOML_LOADER_H
#define SPN_TOML_LOADER_H

#include "codegen/types.h"
#include "core/types.h"

void      spn_toml_loader_init(spn_toml_loader_t* t, sp_mem_t mem, sp_intern_t* intern, const spn_path_roots_t* roots);
spn_err_t spn_codegen_load(spn_toml_loader_t* ctx, spn_path_t path, spn_cg_manifest_t* out);
spn_err_t spn_codegen_load_config(spn_toml_loader_t* ctx, spn_path_t path, spn_cg_config_t* out);

#endif
