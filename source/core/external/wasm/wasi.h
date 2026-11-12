#ifndef SPN_WASM_WASI_H
#define SPN_WASM_WASI_H

#include "sp.h"
#include "spn/core.h"
#include "dag/types.h"
#include "external/wasm/types.h"
#include "wasm_export.h"

spn_err_t       spn_wasi_install(void);
spn_wasi_t* spn_wasi_new(sp_mem_t mem, const spn_path_roots_t* roots, const spn_wasi_mount_t* mounts, u32 count, spn_dag_obs_set_t* obs);
void            spn_wasi_bind(spn_wasi_t* w, wasm_module_inst_t instance);
spn_err_t       spn_wasi_find_mount(const spn_wasi_mount_t* mounts, u32 count, spn_root_t root, sp_str_t sub, const spn_wasi_mount_t** mount);
bool            spn_wasi_stray_write(spn_wasi_t* w, spn_path_t* path);

#endif
