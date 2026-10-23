#ifndef SPN_WASM_H
#define SPN_WASM_H

#include "core/types.h"

#include "external/wasm/types.h"

spn_err_t spn_wasm_init();
void      spn_wasm_thread_exit(void);
spn_err_t spn_wasm_script_open(sp_mem_t mem, spn_pkg_unit_t* unit, spn_path_t module, spn_dag_obs_set_t* obs, spn_wasm_script_t* script);
void      spn_wasm_script_close(spn_wasm_script_t* script);
bool      spn_wasm_script_exports(spn_wasm_script_t* script, sp_str_t name);
spn_err_t spn_wasm_script_call(spn_wasm_script_t* script, sp_str_t name, const spn_wasm_handle_t* handles, u32 count);
bool      spn_wasm_trap(sp_str_t message);

#endif
