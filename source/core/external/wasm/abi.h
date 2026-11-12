#ifndef SPN_WASM_ABI_H
#define SPN_WASM_ABI_H

#include <stdbool.h>
#include "sp.h"
#include "spn.h"
#include "wasm_export.h"

#include "external/wasm/types.h"

struct spn_wasm_handles_t {
  sp_ht(u32, spn_wasm_handle_t) map;
  u32 next;
  spn_t* ctx;
};

typedef struct {
  wasm_module_inst_t instance;
  spn_wasm_handles_t* handles;
} spn_wasm_ctx_t;

typedef u32 spn_wasm_ptr_t;

spn_wasm_ctx_t spn_wasm_ctx(wasm_exec_env_t env);
u32            spn_wasm_add_handle(spn_wasm_handles_t* handles, void* ptr, spn_abi_kind_t kind);
void           spn_wasm_remove_handle(spn_wasm_handles_t* handles, u32 token);
spn_err_t      spn_wasm_resolve_handle(spn_wasm_handles_t* handles, u32 token, spn_abi_kind_t kind, void** ptr);
bool           spn_wasm_read_str(spn_wasm_ctx_t* abi, u32 offset, const c8** out);
spn_wasm_ptr_t spn_wasm_copy_str(spn_wasm_ctx_t* abi, const c8* str);
spn_err_t   spn_abi_io_write(spn_wasm_ctx_t* abi, const c8* path, const c8* contents);
bool        spn_wasm_register_api(void);

#endif
