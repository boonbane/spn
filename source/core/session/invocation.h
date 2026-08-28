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

spn_err_t               spn_session_write_compile_commands(const spn_path_roots_t* roots, spn_session_t* session, spn_path_t path);
sp_da(sp_str_t)         spn_invocation_args(const spn_path_roots_t* roots, sp_mem_t mem, const spn_invocation_t* invocation);
sp_env_var_t            spn_invocation_env_var(const spn_path_roots_t* roots, sp_mem_t mem, spn_invocation_env_t env);
sp_str_t                spn_invocation_to_str(const spn_path_roots_t* roots, sp_mem_t mem, const spn_invocation_t* invocation);
spn_invocation_result_t spn_invocation_run(const spn_path_roots_t* roots, spn_invocation_t* invocation);
spn_invocation_result_t spn_invocation_run_progress(const spn_path_roots_t* roots, spn_invocation_t* invocation, spn_path_t log, sp_io_writer_t* progress);

#endif
