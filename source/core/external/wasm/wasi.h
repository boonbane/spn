#ifndef SPN_WASM_WASI_H
#define SPN_WASM_WASI_H

#include "sp.h"
#include "spn/core.h"
#include "dag/types.h"
#include "wasm_export.h"

typedef struct {
  const c8* guest;
  spn_path_t host;
} spn_wasi_mount_t;

typedef struct spn_wasi_t spn_wasi_t;

spn_err_t       spn_wasi_install(void);
spn_wasi_t* spn_wasi_new(sp_mem_t mem, const spn_path_roots_t* roots, const spn_wasi_mount_t* mounts, u32 count, spn_dag_obs_set_t* obs);
void            spn_wasi_bind(spn_wasi_t* w, wasm_module_inst_t instance);
bool            spn_wasi_resolve(wasm_module_inst_t instance, sp_mem_t mem, sp_str_t guest, spn_path_t* host);
void            spn_wasi_observe_write(wasm_module_inst_t instance, spn_path_t host);

#endif
