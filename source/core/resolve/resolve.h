#ifndef SPN_RESOLVE_RESOLVE_H
#define SPN_RESOLVE_RESOLVE_H

#include "sp.h"
#include "spn/core.h"
#include "resolve/types.h"

#define SPN_RESOLVE_DEFAULT_BUDGET 1000000

void spn_resolve_query_init(sp_mem_t mem, spn_resolve_query_t* query);
void spn_resolve_query_add(spn_resolve_query_t* query, spn_requested_dep_t req);
spn_err_t spn_resolve_from_solver(spn_resolver_t* resolver, spn_resolve_query_t* query);

#endif
