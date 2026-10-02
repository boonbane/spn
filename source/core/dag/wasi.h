#ifndef SPN_DAG_WASI_H
#define SPN_DAG_WASI_H

#include "sp.h"
#include "spn/core.h"
#include "dag/types.h"
#include "wasm_export.h"

typedef struct {
  const c8* guest;
  spn_path_t host;
  bool private;
} spn_dag_wasi_mount_t;

typedef struct spn_dag_wasi_t spn_dag_wasi_t;

spn_err_t       spn_dag_wasi_install(void);
spn_dag_wasi_t* spn_dag_wasi_new(sp_mem_t mem, const spn_path_roots_t* roots, sp_da(spn_path_t) owned, spn_path_t build, const spn_dag_wasi_mount_t* mounts, u32 num_mounts, const spn_path_t* writable, u32 num_writable);
void            spn_dag_wasi_bind(spn_dag_wasi_t* w, wasm_module_inst_t instance);
void            spn_dag_wasi_begin(spn_dag_wasi_t* w, spn_dag_obs_set_t* obs);
void            spn_dag_wasi_end(spn_dag_wasi_t* w);
bool            spn_dag_wasi_stray_write(spn_dag_wasi_t* w, spn_path_t* path);
bool            spn_dag_wasi_build_read(spn_dag_wasi_t* w, spn_path_t* path);
bool            spn_dag_wasi_resolve(wasm_module_inst_t instance, sp_mem_t mem, sp_str_t guest, spn_path_t* host);
bool            spn_dag_wasi_writable(wasm_module_inst_t instance, spn_path_t host);
void            spn_dag_wasi_observe_read(wasm_module_inst_t instance, spn_path_t host);
void            spn_dag_wasi_observe_write(wasm_module_inst_t instance, spn_path_t host);
void            spn_dag_wasi_observe_glob(wasm_module_inst_t instance, spn_path_t dir, sp_str_t pattern);

#endif
