#ifndef SPN_SESSION_INVOCATION_H
#define SPN_SESSION_INVOCATION_H

#include "sp.h"

#include "compiler/types.h"
#include "spn/core.h"
#include "core/types.h"
#include "unit/types.h"

typedef struct {
  sp_ps_output_t result;
  u64 elapsed;
} spn_invocation_result_t;

spn_err_t               spn_build_render_compile(sp_mem_t mem, spn_compile_unit_t* unit, spn_invocation_t* invocation);
spn_err_t               spn_pkg_unit_write_compile_commands(const spn_path_roots_t* roots, spn_pkg_unit_t* unit, sp_str_t path);
spn_err_t               spn_compile_commands_merge(sp_da(sp_str_t) fragments, sp_str_t path);
sp_da(sp_str_t)         spn_invocation_args(const spn_path_roots_t* roots, sp_mem_t mem, const spn_invocation_t* invocation);
sp_env_var_t            spn_invocation_env_var(const spn_path_roots_t* roots, sp_mem_t mem, spn_invocation_env_t env);
sp_str_t                spn_invocation_to_str(sp_mem_t mem, const spn_invocation_t* invocation);
spn_invocation_result_t spn_invocation_run(spn_invocation_t* invocation);

#endif
