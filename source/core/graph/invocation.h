#ifndef SPN_GRAPH_INVOCATION_H
#define SPN_GRAPH_INVOCATION_H

#include "sp.h"

#include "compiler/types.h"
#include "spn/core.h"
#include "spn/events.h"
#include "core/types.h"
#include "toolchain/search.h"
#include "unit/types.h"

typedef struct {
  sp_ps_output_t result;
  u64 elapsed;
} spn_invocation_result_t;

sp_da(sp_str_t)         spn_invocation_args(const spn_path_roots_t* roots, sp_mem_t mem, const spn_invocation_t* invocation);
sp_env_var_t            spn_invocation_env_var(const spn_path_roots_t* roots, sp_mem_t mem, spn_invocation_env_t env);
sp_str_t                spn_invocation_to_str(const spn_path_roots_t* roots, sp_mem_t mem, const spn_invocation_t* invocation);
sp_str_t                spn_invocation_path(const spn_path_roots_t* roots, sp_mem_t mem, const spn_invocation_t* invocation, spn_search_rules_t rules, sp_str_t path);
sp_str_t                spn_invocation_drop_echo(sp_mem_t mem, sp_str_t echo, sp_str_t out, sp_str_t err);
spn_invocation_result_t spn_invocation_run(const spn_path_roots_t* roots, spn_children_t* children, spn_invocation_t* invocation);
spn_invocation_result_t spn_invocation_run_progress(const spn_path_roots_t* roots, spn_children_t* children, spn_invocation_t* invocation, spn_path_t log, sp_io_writer_t* progress);
spn_err_t               spn_invocation_write_log(sp_path_t path, const spn_invocation_result_t* run);
spn_event_run_t         spn_event_run(const spn_path_roots_t* roots, const spn_invocation_t* invocation, const spn_invocation_result_t* run);

#endif
