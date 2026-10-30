#ifndef SPN_SESSION_SESSION_H
#define SPN_SESSION_SESSION_H

#include "sp.h"
#include "spn/core.h"
#include "pkg/types.h"
#include "resolve/types.h"
#include "session/types.h"
#include "target/types.h"

spn_err_t spn_session_init(spn_session_t* session, spn_ctx_t* ctx, sp_mem_t mem, spn_project_t* project, spn_session_config_t config);
spn_err_t spn_session_apply_options(spn_session_t* session, bool* reresolve);
spn_resolved_pkg_t* si_get_root_pkg(spn_session_t* session);

#endif
