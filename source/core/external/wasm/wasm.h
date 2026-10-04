#ifndef SPN_WASM_H
#define SPN_WASM_H

#include "core/types.h"
#include "unit/types.h"

#include "external/wasm/types.h"

typedef struct {
  const spn_user_output_t* declared;
  spn_path_t to;
  sp_err_t err;
} spn_wasm_output_t;

spn_err_t spn_wasm_init();
void      spn_wasm_thread_exit(void);
void      spn_wasm_script_init(spn_wasm_script_t* script, spn_path_t module);
spn_err_t spn_wasm_script_open(spn_wasm_script_t* script, spn_pkg_unit_t* unit);
void      spn_wasm_script_close(spn_wasm_script_t* script, spn_pkg_unit_t* unit);
bool      spn_wasm_script_exports(spn_wasm_script_t* script, sp_str_t name);
spn_err_t spn_wasm_script_call(spn_wasm_script_t* script, spn_pkg_unit_t* unit, sp_str_t name, spn_abi_kind_t kind, void* arg);
bool      spn_wasm_trap_active(spn_pkg_unit_t* unit, sp_str_t message);
spn_err_t spn_wasm_call_export_ex(spn_pkg_unit_t* unit, sp_str_t name, spn_abi_kind_t kind, void* arg, spn_dag_obs_set_t* obs, spn_wasm_output_t* outputs, u32 num_outputs);

#endif
