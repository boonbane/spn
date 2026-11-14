#ifndef SPN_GIT_REMOTE_H
#define SPN_GIT_REMOTE_H

#include "sp.h"
#include "spn/core.h"
#include "external/git.h"
#include "external/net.h"

spn_err_t spn_git_clone(spn_git_transport_t transport, sp_str_t url, sp_str_t path, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure);
spn_err_t spn_git_fetch(spn_git_transport_t transport, sp_str_t repo, sp_str_t url, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure);
spn_err_t spn_git_fetch_rev(spn_git_transport_t transport, sp_str_t repo, sp_str_t url, sp_str_t rev, spn_net_backoff_t backoff, sp_atomic_s32_t* cancelled, spn_git_failure_t* failure);

#endif
